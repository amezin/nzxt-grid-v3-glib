#include "sizedinputstream.h"

struct _GridctlSizedInputStream {
    GFilterInputStream parent_instance;

    gulong min_read_size;
};

G_DEFINE_TYPE(GridctlSizedInputStream, gridctl_sized_input_stream, G_TYPE_FILTER_INPUT_STREAM)

enum { PROP_0, PROP_MIN_READ_SIZE, PROP_COUNT };
static GParamSpec *props[PROP_COUNT];

static void
set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GridctlSizedInputStream *stream = GRIDCTL_SIZED_INPUT_STREAM(object);

    switch (prop_id) {
    case PROP_MIN_READ_SIZE:
        stream->min_read_size = g_value_get_ulong(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GridctlSizedInputStream *stream = GRIDCTL_SIZED_INPUT_STREAM(object);

    switch (prop_id) {
    case PROP_MIN_READ_SIZE:
        g_value_set_ulong(value, stream->min_read_size);
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
                        GridctlSizedInputStream *sized_stream,
                        void *buffer,
                        gsize count)
{
    context->orig_buffer = buffer;
    context->orig_count = count;

    if (count < sized_stream->min_read_size) {
        context->buffer = g_malloc0(sized_stream->min_read_size);
        context->count = sized_stream->min_read_size;
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
sized_read_context_new(GridctlSizedInputStream *sized_stream, void *buffer, gsize count)
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
    GridctlSizedInputStream *sized_stream = GRIDCTL_SIZED_INPUT_STREAM(stream);
    GFilterInputStream *filter_stream = G_FILTER_INPUT_STREAM(sized_stream);
    GInputStream *base_stream = g_filter_input_stream_get_base_stream(filter_stream);

    struct SizedReadContext context;
    sized_read_context_init(&context, sized_stream, buffer, count);

    gssize n_read
        = g_input_stream_read(base_stream, context.buffer, context.count, cancellable, error);

    return sized_read_context_finish(&context, n_read);
}

static void
read_async_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GInputStream *base_stream = G_INPUT_STREAM(source_object);

    g_autoptr(GTask) task = user_data;
    struct SizedReadContext *context = g_task_get_task_data(task);

    GError *error = NULL;

    gssize n_read = g_input_stream_read_finish(base_stream, result, &error);
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
    GridctlSizedInputStream *sized_stream = GRIDCTL_SIZED_INPUT_STREAM(stream);
    GFilterInputStream *filter_stream = G_FILTER_INPUT_STREAM(sized_stream);
    GInputStream *base_stream = g_filter_input_stream_get_base_stream(filter_stream);

    GTask *task = g_task_new(stream, cancellable, callback, user_data);
    g_task_set_source_tag(task, read_async);

    struct SizedReadContext *context = sized_read_context_new(sized_stream, buffer, count);
    g_task_set_task_data(task, context, sized_read_context_free);

    g_input_stream_read_async(base_stream, /* GInputStream *stream */
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
gridctl_sized_input_stream_class_init(GridctlSizedInputStreamClass *class)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(class);
    gobject_class->get_property = get_property;
    gobject_class->set_property = set_property;

    GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS(class);
    input_stream_class->read_fn = read_fn;
    input_stream_class->read_async = read_async;
    input_stream_class->read_finish = read_finish;

    props[PROP_MIN_READ_SIZE] = g_param_spec_ulong(
        "min-read-size", /* const gchar *name */
        "Minimum read size", /* const gchar *nick */
        "Minimum buffer size for read operation", /* const gchar *blurb */
        0, /* gulong minimum */
        G_MAXULONG, /* gulong maximum */
        0, /* gulong default_value */
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS /* GParamFlags flags */);

    g_object_class_install_properties(gobject_class, PROP_COUNT, props);
}

static void
gridctl_sized_input_stream_init(GridctlSizedInputStream *obj)
{
}

GInputStream *
gridctl_sized_input_stream_new(GInputStream *base_stream, gulong min_read_size)
{
    return g_object_new(GRIDCTL_TYPE_SIZED_INPUT_STREAM,
                        "base-stream",
                        base_stream,
                        "min-read-size",
                        min_read_size,
                        NULL);
}
