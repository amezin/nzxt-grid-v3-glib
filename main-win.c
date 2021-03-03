#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>

#include <wing/winginputstream.h>

#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include <hidclass.h>

static const guint16 USB_VENDOR_ID_NZXT = 0x1e71;
static const guint16 USB_PRODUCT_ID_NZXT_GRID_V3 = 0x1711;

static const guint8 NZXT_GRID_STATUS_REPORT_ID = 4;

static USHORT input_report_size;

struct nzxt_grid_status_report {
    guint8 report_id;
    guint8 unknown1[2];
    guint16 rpm;
    guint8 unknown2[2];
    guint8 in_volt;
    guint8 in_centivolt;
    guint8 curr_amp;
    guint8 curr_centiamp;
    guint8 firmware_version_major;
    guint16 firmware_version_minor;
    guint8 firmware_version_patch;
    guint8 channel_index_and_fan_type;
    guint8 unknown4[5]; /* NOLINT(readability-magic-numbers) */
} __attribute__((packed));

static inline guint8
nzxt_grid_status_report_get_fan_type(const struct nzxt_grid_status_report *report)
{
    return report->channel_index_and_fan_type & 0x3;
}

static inline guint8
nzxt_grid_status_report_get_channel(const struct nzxt_grid_status_report *report)
{
    return report->channel_index_and_fan_type >> 4;
}

static inline guint16
nzxt_grid_status_report_get_rpm(const struct nzxt_grid_status_report *report)
{
    return GUINT16_FROM_BE(report->rpm);
}

static void
read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data);

static void
schedule_read(GInputStream *stream)
{
    void *data = g_malloc(input_report_size);

    g_input_stream_read_async(stream,
                              data,
                              input_report_size,
                              G_PRIORITY_DEFAULT,
                              NULL,
                              read_callback,
                              data);
}

static gboolean
schedule_read_source_cb(gpointer user_data)
{
    schedule_read(G_INPUT_STREAM(user_data));
    return FALSE;
}

static void
read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GInputStream *stream = G_INPUT_STREAM(source_object);
    g_autoptr(GError) err = NULL;
    guint8 *data = (guint8 *)user_data;
    gssize read_size = g_input_stream_read_finish(stream, res, &err);

    if (read_size < 0) {
        g_warning("g_input_stream_read_async: %s", err->message);
        g_timeout_add_seconds(1, schedule_read_source_cb, stream);
        return;
    }

    if (data[0] != NZXT_GRID_STATUS_REPORT_ID
        || read_size != sizeof(struct nzxt_grid_status_report)) {
        g_warning("Unexpected report, id = %u, size = %zu\n", data[0], read_size);
        schedule_read(stream);
        return;
    }

    struct nzxt_grid_status_report *status_report = (struct nzxt_grid_status_report *)data;
    g_message("status: channel %u rpm=%u",
              nzxt_grid_status_report_get_channel(status_report),
              nzxt_grid_status_report_get_rpm(status_report));
    schedule_read(stream);
}

static void
device_info_list_cleanup(HDEVINFO *list)
{
    if (*list == INVALID_HANDLE_VALUE) {
        return;
    }

    SetupDiDestroyDeviceInfoList(*list);
    *list = INVALID_HANDLE_VALUE;
}

static void
handle_cleanup(HANDLE *handle)
{
    if (*handle == INVALID_HANDLE_VALUE) {
        return;
    }

    CloseHandle(*handle);
    *handle = INVALID_HANDLE_VALUE;
}

static gchar *
wchar_t_to_utf8(const wchar_t *str)
{
    g_autoptr(GError) error = NULL;
    gchar *str_utf8 = g_utf16_to_utf8(str, -1, NULL, NULL, &error);
    if (!str_utf8) {
        g_warning("g_utf16_to_utf8: %s", error->message);
    }
    return str_utf8;
}

static gchar *
find_grid_device()
{
    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    __attribute__((cleanup(device_info_list_cleanup))) HDEVINFO device_info_list
        = SetupDiGetClassDevsA(&hid_guid, /* CONST GUID *ClassGuid */
                               NULL, /* PCSTR Enumerator */
                               NULL, /* HWND hwndParent */
                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE /* DWORD Flags */);

    if (device_info_list == INVALID_HANDLE_VALUE) {
        g_autofree gchar *err_message = g_win32_error_message(GetLastError());
        g_warning("SetupDiGetClassDevsA: %s", err_message);
        return NULL;
    }

    SP_DEVICE_INTERFACE_DATA device_interface_data = {
        .cbSize = sizeof(SP_DEVICE_INTERFACE_DATA),
    };

    for (int index = 0; SetupDiEnumDeviceInterfaces(
             device_info_list, /* HDEVINFO DeviceInfoSet */
             NULL, /* PSP_DEVINFO_DATA DeviceInfoData */
             &hid_guid, /* CONST GUID *InterfaceClassGuid */
             index, /* DWORD MemberIndex */
             &device_interface_data /* PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData */);
         index++)
    {
        DWORD device_interface_detail_size = 0;
        SetupDiGetDeviceInterfaceDetailW(
            device_info_list, /* HDEVINFO DeviceInfoSet */
            &device_interface_data, /* PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData */
            NULL, /* PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData */
            0, /* DWORD DeviceInterfaceDetailDataSize */
            &device_interface_detail_size, /* PDWORD RequiredSize */
            NULL /* PSP_DEVINFO_DATA DeviceInfoData */);
        if (device_interface_detail_size == 0) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("SetupDiGetDeviceInterfaceDetailW: %s", err_message);
            continue;
        }

        g_autofree SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data
            = g_malloc(device_interface_detail_size);
        device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
                device_info_list, /* HDEVINFO DeviceInfoSet */
                &device_interface_data, /* PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData */
                device_interface_detail_data, /* PSP_DEVICE_INTERFACE_DETAIL_DATA_W
                                                 DeviceInterfaceDetailData */
                device_interface_detail_size, /* DWORD DeviceInterfaceDetailDataSize */
                NULL, /* PDWORD RequiredSize */
                NULL /* PSP_DEVINFO_DATA DeviceInfoData */))
        {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("SetupDiGetDeviceInterfaceDetailW: %s", err_message);
            continue;
        }

        __attribute__((cleanup(handle_cleanup))) HANDLE device
            = CreateFileW(device_interface_detail_data->DevicePath, /* LPCSTR lpFileName */
                          0, /* DWORD dwDesiredAccess */
                          FILE_SHARE_READ | FILE_SHARE_WRITE, /* DWORD dwShareMode */
                          NULL, /* LPSECURITY_ATTRIBUTES lpSecurityAttributes */
                          OPEN_EXISTING, /* DWORD dwCreationDisposition */
                          0, /* DWORD dwFlagsAndAttributes */
                          NULL /* HANDLE hTemplateFile */);
        if (device == INVALID_HANDLE_VALUE) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("CreateFileW: %s", err_message);
            continue;
        }

        HIDD_ATTRIBUTES attributes = {
            .Size = sizeof(HIDD_ATTRIBUTES),
        };
        if (!HidD_GetAttributes(device, &attributes)) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("HidD_GetAttributes: %s", err_message);
            continue;
        }

        if (attributes.VendorID == USB_VENDOR_ID_NZXT
            && attributes.ProductID == USB_PRODUCT_ID_NZXT_GRID_V3)
        {
            return wchar_t_to_utf8(device_interface_detail_data->DevicePath);
        }
    }

    return NULL;
}

static void
hid_preparsed_data_cleanup(PHIDP_PREPARSED_DATA *data)
{
    if (*data == NULL) {
        return;
    }

    if (HidD_FreePreparsedData(*data)) {
        *data = NULL;
    } else {
        g_autofree gchar *err_message = g_win32_error_message(GetLastError());
        g_warning("HidD_FreePreparsedData: %s", err_message);
    }
}

static HANDLE
steal_handle(HANDLE *value)
{
    HANDLE tmp = *value;
    *value = INVALID_HANDLE_VALUE;
    return tmp;
}

static GInputStream *
open_device(const gchar *path)
{
    g_autoptr(GError) error = NULL;
    g_autofree wchar_t *path_wchar = g_utf8_to_utf16(path, -1, NULL, NULL, &error);
    if (!path_wchar) {
        g_warning("g_utf8_to_utf16: %s", error->message);
        return NULL;
    }

    __attribute__((cleanup(handle_cleanup))) HANDLE device
        = CreateFileW(path_wchar, /* LPCSTR lpFileName */
                      GENERIC_READ | GENERIC_WRITE, /* DWORD dwDesiredAccess */
                      FILE_SHARE_READ | FILE_SHARE_WRITE, /* DWORD dwShareMode */
                      NULL, /* LPSECURITY_ATTRIBUTES lpSecurityAttributes */
                      OPEN_EXISTING, /* DWORD dwCreationDisposition */
                      FILE_FLAG_OVERLAPPED, /* DWORD dwFlagsAndAttributes */
                      NULL /* HANDLE hTemplateFile */);
    if (device == INVALID_HANDLE_VALUE) {
        g_autofree gchar *err_message = g_win32_error_message(GetLastError());
        g_warning("CreateFileW: %s", err_message);
        return NULL;
    }

    __attribute__((cleanup(hid_preparsed_data_cleanup))) PHIDP_PREPARSED_DATA preparsed_data = NULL;
    if (!HidD_GetPreparsedData(device, &preparsed_data)) {
        g_autofree gchar *err_message = g_win32_error_message(GetLastError());
        g_warning("HidD_GetPreparsedData: %s", err_message);
        return NULL;
    }

    HIDP_CAPS caps;
    if (HidP_GetCaps(preparsed_data, &caps) != HIDP_STATUS_SUCCESS) {
        g_warning("HidP_GetCaps failed");
        return NULL;
    }

    input_report_size = caps.InputReportByteLength;

    g_message("Device %s opened", path);

    return wing_input_stream_new(steal_handle(&device), TRUE);
}

int
main(void)
{
    g_autofree gchar *device_path = find_grid_device();
    if (!device_path) {
        g_warning("Can't find NZXT Grid device");
        return EXIT_FAILURE;
    }

    g_autoptr(GInputStream) input_stream = open_device(device_path);
    if (!input_stream) {
        g_warning("Can't open device");
        return EXIT_FAILURE;
    }

    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    schedule_read(input_stream);
    g_main_loop_run(loop);

    return EXIT_SUCCESS;
}
