#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include <gudev/gudev.h>

static const guint16 USB_VENDOR_ID_NZXT = 0x1e71;
static const guint16 USB_PRODUCT_ID_NZXT_GRID_V3 = 0x1711;

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
	guint8 channel_index_and_fan_type;
	guint8 unknown4[5];
} __attribute__((packed));

static inline guint8 nzxt_grid_status_report_get_fan_type(const struct nzxt_grid_status_report *report)
{
    return report->channel_index_and_fan_type & 0x3;
}

static inline guint8 nzxt_grid_status_report_get_channel(const struct nzxt_grid_status_report *report)
{
    return report->channel_index_and_fan_type >> 4;
}

static inline guint16 nzxt_grid_status_report_get_rpm(const struct nzxt_grid_status_report *report)
{
    return GUINT16_FROM_BE(report->rpm);
}

static void read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data);

static void schedule_read(GInputStream *stream)
{
    static const gsize BUFFER_SIZE = 64;
    guint8 *data = (guint8 *)g_malloc(BUFFER_SIZE);

    g_input_stream_read_async(stream, data, BUFFER_SIZE, G_PRIORITY_DEFAULT, NULL, read_callback, data);
}

static gboolean schedule_read_source_cb(gpointer user_data)
{
    schedule_read(G_INPUT_STREAM(user_data));
    return FALSE;
}

static void read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
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

    if (data[0] != NZXT_GRID_STATUS_REPORT_ID || read_size != sizeof(struct nzxt_grid_status_report)) {
        g_warning("Unexpected report, id = %u, size = %zu\n", data[0], read_size);
        schedule_read(stream);
        return;
    }

    struct nzxt_grid_status_report *status_report = (struct nzxt_grid_status_report *)data;
    g_message(
        "status: channel %u rpm=%u",
        nzxt_grid_status_report_get_channel(status_report),
        nzxt_grid_status_report_get_rpm(status_report)
    );
    schedule_read(stream);
}

static gchar *find_grid_device()
{
    g_autoptr(GUdevClient) client = g_udev_client_new(NULL);
    g_autolist(GUdevDevice) devices = g_udev_client_query_by_subsystem(client, "hidraw");

    for (GList *l = devices; l != NULL; l = l->next) {
        GUdevDevice *hidraw_dev = G_UDEV_DEVICE(l->data);
        g_autoptr(GUdevDevice) hid_dev = g_udev_device_get_parent_with_subsystem(hidraw_dev, "hid", NULL);

        if (!hid_dev) {
            g_warning("Can't find parent hid device for hidraw device");
            continue;
        }

        const gchar *id = g_udev_device_get_property(hid_dev, "HID_ID");
        unsigned bus_type;
        unsigned short vendor_id, product_id;

        int n_fields = sscanf(id, "%x:%hx:%hx", &bus_type, &vendor_id, &product_id);
        if (n_fields != 3) {
            g_warning("Can't parse %s as HID ID, skipping device", id);
            continue;
        }

        g_message("Found device %x:%x", vendor_id, product_id);

        if (vendor_id == USB_VENDOR_ID_NZXT && product_id == USB_PRODUCT_ID_NZXT_GRID_V3) {
            return g_strdup(g_udev_device_get_device_file(hidraw_dev));
        }
    }

    return NULL;
}

static GInputStream *open_device(const gchar *path)
{
    int fd = open(path, O_RDWR);

    if (fd < 0) {
        perror("open");
        return NULL;
    }

    return g_unix_input_stream_new(fd, TRUE);
}

int main(void)
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
