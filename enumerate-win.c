#include <stdio.h>

#include <glib.h>

#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

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

int
main(void)
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
        return EXIT_FAILURE;
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

        g_autofree gchar *device_path = wchar_t_to_utf8(device_interface_detail_data->DevicePath);
        g_message("Found device: %s", device_path);

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

        g_message("Vendor = %#06x Product = %#06x", attributes.VendorID, attributes.ProductID);

        static const gsize str_buf_size = 4093; /* Must be <= 4093 */
        g_autofree wchar_t *wstr_buf = g_malloc(str_buf_size);

        if (!HidD_GetManufacturerString(device, wstr_buf, str_buf_size)) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("HidD_GetManufacturerString: %s", err_message);
            continue;
        }

        g_autofree gchar *vendor_str = wchar_t_to_utf8(wstr_buf);
        g_message("Vendor = %s", vendor_str);

        if (!HidD_GetProductString(device, wstr_buf, str_buf_size)) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("HidD_GetProductString: %s", err_message);
            continue;
        }

        g_autofree gchar *product_str = wchar_t_to_utf8(wstr_buf);
        g_message("Product = %s", product_str);

        if (!HidD_GetSerialNumberString(device, wstr_buf, str_buf_size)) {
            g_autofree gchar *err_message = g_win32_error_message(GetLastError());
            g_warning("HidD_GetSerialNumberString: %s", err_message);
            continue;
        }

        g_autofree gchar *serial_str = wchar_t_to_utf8(wstr_buf);
        g_message("Serial Number = %s", serial_str);
    }

    return EXIT_SUCCESS;
}
