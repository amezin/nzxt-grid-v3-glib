#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define GRIDCTL_TYPE_SIZED_INPUT_STREAM (gridctl_sized_input_stream_get_type())
G_DECLARE_FINAL_TYPE(GridctlSizedInputStream,
                     gridctl_sized_input_stream,
                     GRIDCTL,
                     SIZED_INPUT_STREAM,
                     GFilterInputStream)

GInputStream *
gridctl_sized_input_stream_new(GInputStream *base_stream, gulong min_read_size);

G_END_DECLS
