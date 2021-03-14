#include "winhidinputstream.h"

struct _GridctlWinHidInputStream {
    WingInputStream parent_instance;

    gulong input_report_length;

    void *allocated_buffer;

    void *buffer;
    void *orig_buffer;
    gsize orig_count;
};

G_DEFINE_TYPE(GridctlWinHidInputStream, gridctl_win_hid_input_stream, WING_TYPE_INPUT_STREAM)

enum { PROP_0, PROP_INPUT_REPORT_LENGTH, PROP_COUNT };
static GParamSpec *props[PROP_COUNT];

static void
set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GridctlWinHidInputStream *stream = GRIDCTL_WIN_HID_INPUT_STREAM(object);

    switch (prop_id) {
    case PROP_INPUT_REPORT_LENGTH:
        stream->input_report_length = g_value_get_ulong(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GridctlWinHidInputStream *stream = GRIDCTL_WIN_HID_INPUT_STREAM(object);

    switch (prop_id) {
    case PROP_INPUT_REPORT_LENGTH:
        g_value_set_ulong(value, stream->input_report_length);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
finalize(GObject *object)
{
    GridctlWinHidInputStream *stream = GRIDCTL_WIN_HID_INPUT_STREAM(object);

    g_clear_pointer(&stream->allocated_buffer, g_free);

    G_OBJECT_CLASS(gridctl_win_hid_input_stream_parent_class)->finalize(object);
}

static gsize
read_init(GridctlWinHidInputStream *win_hid_stream, void *buffer, gsize count)
{
    g_return_val_if_fail(win_hid_stream->buffer == NULL, 0);
    g_return_val_if_fail(win_hid_stream->orig_buffer == NULL, 0);
    g_return_val_if_fail(win_hid_stream->orig_count == 0, 0);

    win_hid_stream->orig_buffer = buffer;
    win_hid_stream->orig_count = count;

    if (count >= win_hid_stream->input_report_length) {
        win_hid_stream->buffer = buffer;
        return count;
    }

    if (!win_hid_stream->allocated_buffer) {
        win_hid_stream->allocated_buffer = g_malloc(win_hid_stream->input_report_length);
    }

    win_hid_stream->buffer = win_hid_stream->allocated_buffer;
    return win_hid_stream->input_report_length;
}

static void
read_reset(GridctlWinHidInputStream *win_hid_stream)
{
    win_hid_stream->buffer = NULL;
    win_hid_stream->orig_buffer = NULL;
    win_hid_stream->orig_count = 0;
}

static gssize
read_complete(GridctlWinHidInputStream *win_hid_stream, gssize n_read)
{
    g_return_val_if_fail(win_hid_stream->buffer != NULL, -1);
    g_return_val_if_fail(win_hid_stream->orig_buffer != NULL, -1);

    if (n_read <= 0) {
        read_reset(win_hid_stream);
        return n_read;
    }

    gboolean can_use_memcpy = (win_hid_stream->buffer != win_hid_stream->orig_buffer);

    if (*(guint8 *)win_hid_stream->buffer == 0) {
        /* https://github.com/libusb/hidapi/blob/0ea5e9502f31a1e4dbc71567c0f9a781930f1d38/windows/hid.c#L770
         *
         * If report numbers aren't being used, but Windows sticks a report number (0x0) on the
         * beginning of the report anyway. To make this work like the other platforms, and to make
         * it work more like the HID spec, we'll skip over this byte.
         */
        win_hid_stream->buffer = win_hid_stream->buffer + 1;
        n_read -= 1;
    }

    if (win_hid_stream->buffer == win_hid_stream->orig_buffer || n_read == 0) {
        read_reset(win_hid_stream);
        return n_read;
    }

    gsize u_n_read = (gsize)n_read;
    if (u_n_read > win_hid_stream->orig_count) {
        u_n_read = win_hid_stream->orig_count;
    }

    if (can_use_memcpy) {
        memcpy(win_hid_stream->orig_buffer, win_hid_stream->buffer, u_n_read);
    } else {
        memmove(win_hid_stream->orig_buffer, win_hid_stream->buffer, u_n_read);
    }

    read_reset(win_hid_stream);
    return (gssize)u_n_read;
}

static gssize
read_fn(GInputStream *stream, void *buffer, gsize count, GCancellable *cancellable, GError **error)
{
    GridctlWinHidInputStream *win_hid_stream = GRIDCTL_WIN_HID_INPUT_STREAM(stream);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    gsize real_count = read_init(win_hid_stream, buffer, count);

    gssize n_read = base_class->read_fn(stream, /* GInputStream *stream */
                                        win_hid_stream->buffer, /* void *buffer */
                                        real_count, /* gsize count */
                                        cancellable, /* GCancellable *cancellable */
                                        error /* GError **error */);

    return read_complete(win_hid_stream, n_read);
}

static void
read_async_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GridctlWinHidInputStream *win_hid_stream = GRIDCTL_WIN_HID_INPUT_STREAM(source_object);
    GInputStream *stream = G_INPUT_STREAM(win_hid_stream);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    g_autoptr(GTask) task = user_data;
    GError *error = NULL;

    gssize n_read = base_class->read_finish(stream, result, &error);
    n_read = read_complete(win_hid_stream, n_read);

    if (error) {
        g_task_return_error(task, error);
    } else {
        g_task_return_int(task, n_read);
    }
}

static void
read_async(GInputStream *stream,
           void *buffer,
           gsize count,
           int io_priority,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    GridctlWinHidInputStream *win_hid_stream = GRIDCTL_WIN_HID_INPUT_STREAM(stream);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    GTask *task = g_task_new(stream, cancellable, callback, user_data);
    g_task_set_source_tag(task, read_async);

    gsize real_count = read_init(win_hid_stream, buffer, count);

    base_class->read_async(stream, /* GInputStream *stream */
                           win_hid_stream->buffer, /* void *buffer */
                           real_count, /* gsize count */
                           io_priority, /* int io_priority */
                           cancellable, /* GCancellable *cancellable */
                           read_async_callback, /* GAsyncReadyCallback callback */
                           task /* gpointer user_data */);
}

static gssize
read_finish(GInputStream *stream, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, stream), -1);
    return g_task_propagate_int(G_TASK(result), error);
}

static void
gridctl_win_hid_input_stream_class_init(GridctlWinHidInputStreamClass *class)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(class);
    gobject_class->get_property = get_property;
    gobject_class->set_property = set_property;
    gobject_class->finalize = finalize;

    GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS(class);
    input_stream_class->read_fn = read_fn;
    input_stream_class->read_async = read_async;
    input_stream_class->read_finish = read_finish;

    props[PROP_INPUT_REPORT_LENGTH] = g_param_spec_ulong(
        "input-report-length", /* const gchar *name */
        "Input Report Length", /* const gchar *nick */
        "Maximum size, in bytes, of all the input reports", /* const gchar *blurb */
        0, /* gulong minimum */
        G_MAXULONG, /* gulong maximum */
        0, /* gulong default_value */
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
            | G_PARAM_STATIC_STRINGS /* GParamFlags flags */);

    g_object_class_install_properties(gobject_class, PROP_COUNT, props);
}

static void
gridctl_win_hid_input_stream_init(GridctlWinHidInputStream *obj)
{
}

GInputStream *
gridctl_win_hid_input_stream_new(void *handle, gboolean close_handle, gulong input_report_length)
{
    return g_object_new(GRIDCTL_TYPE_WIN_HID_INPUT_STREAM,
                        "handle",
                        handle,
                        "close-handle",
                        close_handle,
                        "input-report-length",
                        input_report_length,
                        NULL);
}
