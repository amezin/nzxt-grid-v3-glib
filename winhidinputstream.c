#include "winhidinputstream.h"

struct _GridctlWinHidInputStream {
    WingInputStream parent_instance;

    gulong input_report_length;
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

struct SizedReadContext {
    void *buffer;
    gsize count;
    void *orig_buffer;
    gsize orig_count;
};

static void
sized_read_context_init(struct SizedReadContext *context,
                        GridctlWinHidInputStream *sized_stream,
                        void *buffer,
                        gsize count)
{
    context->orig_buffer = buffer;
    context->orig_count = count;

    if (count < sized_stream->input_report_length) {
        context->buffer = g_malloc0(sized_stream->input_report_length);
        context->count = sized_stream->input_report_length;
    } else {
        context->buffer = buffer;
        context->count = count;
    }
}

static void
sized_read_context_dispose(struct SizedReadContext *context)
{
    if (context->buffer && context->buffer != context->orig_buffer) {
        g_free(context->buffer);
        context->buffer = NULL;
    }
}

static gssize
sized_read_context_finish(struct SizedReadContext *context, gssize n_read)
{
    g_return_val_if_fail(context->buffer != NULL, -1);

    if (n_read <= 0 || context->buffer == context->orig_buffer) {
        sized_read_context_dispose(context);
        return n_read;
    }

    gsize u_n_read = (gsize)n_read;
    if (u_n_read <= context->orig_count) {
        memcpy(context->orig_buffer, context->buffer, u_n_read);
        sized_read_context_dispose(context);
        return n_read;
    }

    memcpy(context->orig_buffer, context->buffer, context->orig_count);
    sized_read_context_dispose(context);
    return (gssize)context->orig_count;
}

static struct SizedReadContext *
sized_read_context_new(GridctlWinHidInputStream *sized_stream, void *buffer, gsize count)
{
    struct SizedReadContext *context = g_new(struct SizedReadContext, 1);
    sized_read_context_init(context, sized_stream, buffer, count);
    return context;
}

static void
sized_read_context_free(gpointer ptr)
{
    if (!ptr) {
        return;
    }

    struct SizedReadContext *context = ptr;
    sized_read_context_dispose(context);

    g_free(ptr);
}

static gssize
read_fn(GInputStream *stream, void *buffer, gsize count, GCancellable *cancellable, GError **error)
{
    GridctlWinHidInputStream *sized_stream = GRIDCTL_WIN_HID_INPUT_STREAM(stream);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    struct SizedReadContext context;
    sized_read_context_init(&context, sized_stream, buffer, count);

    gssize n_read = base_class->read_fn(stream, context.buffer, context.count, cancellable, error);

    return sized_read_context_finish(&context, n_read);
}

static void
read_async_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GInputStream *stream = G_INPUT_STREAM(source_object);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    g_autoptr(GTask) task = user_data;
    struct SizedReadContext *context = g_task_get_task_data(task);

    GError *error = NULL;

    gssize n_read = base_class->read_finish(stream, result, &error);
    n_read = sized_read_context_finish(context, n_read);

    if (n_read < 0) {
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
    GridctlWinHidInputStream *sized_stream = GRIDCTL_WIN_HID_INPUT_STREAM(stream);
    GInputStreamClass *base_class = G_INPUT_STREAM_CLASS(gridctl_win_hid_input_stream_parent_class);

    GTask *task = g_task_new(stream, cancellable, callback, user_data);
    g_task_set_source_tag(task, read_async);

    struct SizedReadContext *context = sized_read_context_new(sized_stream, buffer, count);
    g_task_set_task_data(task, context, sized_read_context_free);

    base_class->read_async(stream, /* GInputStream *stream */
                           context->buffer, /* void *buffer */
                           context->count, /* gsize count */
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
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS /* GParamFlags flags */);

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
