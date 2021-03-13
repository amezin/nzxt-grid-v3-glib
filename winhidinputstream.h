#pragma once

#include <wing/winginputstream.h>

G_BEGIN_DECLS

#define GRIDCTL_TYPE_WIN_HID_INPUT_STREAM (gridctl_win_hid_input_stream_get_type())
G_DECLARE_FINAL_TYPE(GridctlWinHidInputStream,
                     gridctl_win_hid_input_stream,
                     GRIDCTL,
                     WIN_HID_INPUT_STREAM,
                     WingInputStream)

GInputStream *
gridctl_win_hid_input_stream_new(void *handle, gboolean close_handle, gulong input_report_length);

G_END_DECLS
