#include <stdio.h>

#include <gio/gio.h>

#include <gudev/gudev.h>

static const char *
ensure_nonnull(const char *value)
{
    if (value == NULL)
        return "<null>";

    return value;
}

static void
print_device_info(GUdevDevice *dev, const char *indent)
{
#define PRINT_FUNC_RESULT(f) printf("%s" #f "()=%s\n", indent, ensure_nonnull(f(dev)))

    PRINT_FUNC_RESULT(g_udev_device_get_subsystem);
    PRINT_FUNC_RESULT(g_udev_device_get_devtype);
    PRINT_FUNC_RESULT(g_udev_device_get_name);
    PRINT_FUNC_RESULT(g_udev_device_get_number);
    PRINT_FUNC_RESULT(g_udev_device_get_sysfs_path);
    PRINT_FUNC_RESULT(g_udev_device_get_driver);
    PRINT_FUNC_RESULT(g_udev_device_get_action);
    PRINT_FUNC_RESULT(g_udev_device_get_device_file);

    for (const gchar *const *i = g_udev_device_get_property_keys(dev); *i != NULL; i++) {
        printf("%s%s=%s\n", indent, *i, ensure_nonnull(g_udev_device_get_property(dev, *i)));
    }

    g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(dev);
    if (!parent)
        return;

    g_autofree gchar *parent_indent = g_strdup_printf("    %s", indent);
    print_device_info(parent, parent_indent);
}

int
main(void)
{
    g_autoptr(GUdevClient) client = g_udev_client_new(NULL);
    g_autolist(GUdevDevice) devices = g_udev_client_query_by_subsystem(client, "hidraw");

    for (GList *l = devices; l != NULL; l = l->next) {
        GUdevDevice *dev = G_UDEV_DEVICE(l->data);

        print_device_info(dev, "");
        printf("\n");
    }

    return EXIT_SUCCESS;
}
