#include <stdio.h>

#include <gio/gio.h>
#include <gusb.h>

static const guint16 USB_VENDOR_ID_NZXT = 0x1e71;
static const guint16 USB_PRODUCT_ID_NZXT_GRID_V3 = 0x1711;

static const guint8 NZXT_GRID_ENDPOINT_ADDRESS_IN = 0x81;
static const guint8 NZXT_GRID_ENDPOINT_ADDRESS_OUT = 0x1;

static const guint8 NZXT_GRID_STATUS_REPORT_ID = 4;

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
	guint8 type : 2;
	guint8 unknown3 : 2;
	guint8 channel : 4;
	guint8 unknown4[5];
} __attribute__((packed));

static int my_perror(const char *func, GError *err)
{
    g_printerr("%s: %s\n", func, (err && err->message) ? err->message : "unknown error");
    return EXIT_FAILURE;
}

int main(void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GUsbContext) usb_ctx = NULL;
    g_autoptr(GUsbDevice) usb_dev = NULL;
    g_autoptr(GUsbInterface) usb_iface = NULL;

    guint8 data_in[64];
    gsize data_in_size;

    usb_ctx = g_usb_context_new(&err);
    if (!usb_ctx)
        return my_perror("g_usb_context_new", err);

    usb_dev = g_usb_context_find_by_vid_pid(usb_ctx, USB_VENDOR_ID_NZXT, USB_PRODUCT_ID_NZXT_GRID_V3, &err);
    if (!usb_dev)
        return my_perror("g_usb_context_find_by_vid_pid", err);

    usb_iface = g_usb_device_get_interface(usb_dev, G_USB_DEVICE_CLASS_HID, 0, 0, &err);
    if (!usb_iface)
        return my_perror("g_usb_device_get_interface", err);

    if (!g_usb_device_open(usb_dev, &err))
        return my_perror("g_usb_device_open", err);

    if (!g_usb_device_claim_interface(usb_dev, g_usb_interface_get_number(usb_iface), G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, &err))
        return my_perror("g_usb_device_claim_interface", err);

    for (;;) {
        if (!g_usb_device_interrupt_transfer(usb_dev, NZXT_GRID_ENDPOINT_ADDRESS_IN, data_in, sizeof(data_in), &data_in_size, 0, NULL, &err))
            return my_perror("g_usb_device_interrupt_transfer", err);

        if (data_in[0] != NZXT_GRID_STATUS_REPORT_ID || data_in_size != sizeof(struct nzxt_grid_status_report)) {
            fprintf(stderr, "Unexpected report, id = %u, size = %zu\n", data_in[0], data_in_size);
            continue;
        }

        struct nzxt_grid_status_report *status_report = (struct nzxt_grid_status_report *)data_in;
        fprintf(stderr, "status: channel %u rpm=%u\n", status_report->channel, GUINT16_FROM_BE(status_report->rpm));
    }

    if (!g_usb_device_close(usb_dev, &err))
        return my_perror("g_usb_device_close", err);

    return EXIT_SUCCESS;
}
