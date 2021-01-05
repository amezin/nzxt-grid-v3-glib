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

static void read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data);

static void schedule_read(GUsbDevice *usb_dev)
{
    static const gsize BUFFER_SIZE = 64;
    guint8 *data = (guint8 *)g_malloc(BUFFER_SIZE);

    g_usb_device_interrupt_transfer_async(usb_dev, NZXT_GRID_ENDPOINT_ADDRESS_IN, data, BUFFER_SIZE, 0, NULL, read_callback, data);
}

static gboolean schedule_read_source_cb(gpointer user_data)
{
    schedule_read(G_USB_DEVICE(user_data));
    return FALSE;
}

static void read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GUsbDevice *usb_dev = G_USB_DEVICE(source_object);
    g_autoptr(GError) err = NULL;
    guint8 *data = (guint8 *)user_data;
    gssize read_size = g_usb_device_interrupt_transfer_finish(usb_dev, res, &err);

    if (read_size < 0) {
        my_perror("g_usb_device_interrupt_transfer_async", err);
        g_timeout_add_seconds(1, schedule_read_source_cb, usb_dev);
        return;
    }

    if (data[0] != NZXT_GRID_STATUS_REPORT_ID || read_size != sizeof(struct nzxt_grid_status_report)) {
        fprintf(stderr, "Unexpected report, id = %u, size = %zu\n", data[0], read_size);
        schedule_read(usb_dev);
        return;
    }

    struct nzxt_grid_status_report *status_report = (struct nzxt_grid_status_report *)data;
    fprintf(stderr, "status: channel %u rpm=%u\n", status_report->channel, GUINT16_FROM_BE(status_report->rpm));
    schedule_read(usb_dev);
}

int main(void)
{
    g_autoptr(GError) err = NULL;
    g_autoptr(GMainLoop) loop = NULL;
    g_autoptr(GUsbContext) usb_ctx = NULL;
    g_autoptr(GUsbDevice) usb_dev = NULL;
    g_autoptr(GUsbInterface) usb_iface = NULL;

    loop = g_main_loop_new(NULL, FALSE);

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

    schedule_read(usb_dev);
    g_main_loop_run(loop);

    if (!g_usb_device_close(usb_dev, &err))
        return my_perror("g_usb_device_close", err);

    return EXIT_SUCCESS;
}
