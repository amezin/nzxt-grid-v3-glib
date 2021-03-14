#pragma once

#include <glib.h>

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
