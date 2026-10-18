/* disk.h — disks and partitions for the Disks app (see docs/SYSTEM.md, "Disks").
 *
 * disk.c reads without root: /sys/block, the udev database (/run/udev/data/b<major>:<minor>) and
 * /proc/self/mountinfo. It is used by lord-server (lord.disks, js_disk.c) and by the admin helper
 * (disk_admin.c), which validates with these same functions on the root side.
 * smart.c holds the parsers (pure functions) for ATA SMART and the NVMe health log; the helper
 * reads them with SG_IO / NVME_IOCTL_ADMIN_CMD (action disk-smart). */
#ifndef LORD_DISK_H
#define LORD_DISK_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../admin.h"

/* Block device name as the kernel gives it: sda, sda1, nvme0n1p2, mmcblk0p1, vdb, loop3
 * (lowercase letters and digits, starts with a letter, up to 31). */
int  disk_valid_name(const char *name);
/* 1 if it is a partition (/sys/class/block/<n>/partition exists); its disk goes in disk. */
int  disk_parent(const char *name, char *disk, size_t n);
/* 1 if the (whole) disk holds something of the system: /, /usr, /var, /home, /boot, /boot/efi, the
 * live medium, an active swap, or a dm/md on top of it that holds any of those. */
int  disk_is_system(const char *disk);
/* 1 if removable: removable=1 in sysfs, connected over USB, or a card (mmc). */
int  disk_is_removable(const char *disk);
/* udev database property for that device ("ID_FS_TYPE"…); "" if missing. */
int  disk_udev_prop(const char *name, const char *key, char *out, size_t n);
/* File system label as is (ID_FS_LABEL_ENC with the \xNN decoded); "" if none. */
void disk_fs_label(const char *name, char *out, size_t n);
/* Size in bytes (/sys/class/block/<n>/size × 512). */
uint64_t disk_size(const char *name);
/* Mount points of the device (separated by \n) from mountinfo; returns how many. */
int  disk_mounts(const char *name, char *out, size_t n);

/* List for JS: [{ name, model, vendor, serial, size, removable, transport, rotational, ro,
 * system, table, fstype, label, uuid, mountpoint, used, avail, partitions: [{ name, number, size,
 * fstype, label, uuid, partlabel, mountpoint, used, avail, start }] }]. malloc'd; NULL on failure. */
char *disk_list_json(void);
/* Format and rename programs present on this machine: {"fat32":1,"exfat":1,"ext4":1,"ntfs":0} */
char *disk_tools_json(void);

/* Change notifications while the app is open: one fd (epoll over mountinfo and udev events) for
 * the main loop. disk_watch(1) opens it, disk_watch(0) closes it; -1 when not open. */
int  disk_watch(int on);
int  disk_watch_fd(void);
/* Reads what arrived; 1 if disks or mounts changed (JS must be told). */
int  disk_watch_dispatch(void);

/* ---- SMART (smart.c, pure functions: tests/smart_test.c) ---- */

enum { SMART_UNKNOWN = 0, SMART_OK = 1, SMART_WARN = 2, SMART_FAIL = 3 };
enum { SMART_ATA = 1, SMART_NVME = 2 };

typedef struct {
    uint8_t id, value, worst, thresh;
    uint16_t flags;                     /* bit 0: pre-fail */
    uint64_t raw;                       /* 48 bits */
    int failing;                        /* value <= threshold (threshold != 0) */
} SmartAttr;

typedef struct {
    int kind;                           /* SMART_ATA / SMART_NVME */
    int health;                         /* SMART_OK… */
    int checksum_ok;                    /* ATA: the 512 bytes sum to 0 */
    int status;                         /* ATA RETURN STATUS: 1 good, 0 threshold exceeded, -1 unknown */
    int temp_c;                         /* -1 if unknown */
    int64_t power_on_hours, power_cycles;           /* -1 if unknown */
    int64_t reallocated, pending, uncorrectable;    /* ATA 5, 197, 198 (-1 if absent) */
    int nattr;
    SmartAttr attr[30];
    /* NVMe */
    int crit_warning, spare, spare_thresh, used_pct;
    uint64_t data_read, data_written, media_errors, unsafe_shutdowns;   /* data_*: bytes */
} SmartReport;

/* data = SMART READ DATA (512 bytes); thresh = READ THRESHOLDS (512) or NULL; status = 1/0/-1. */
int  smart_ata_parse(const uint8_t data[512], const uint8_t *thresh, int status, SmartReport *r);
/* log = NVMe log page 02h (SMART / Health Information, 512 bytes). */
int  smart_nvme_parse(const uint8_t log[512], SmartReport *r);
/* Summary as JSON (without the attributes): {kind, health, temp, hours, cycles, ...}. */
int  smart_summary_json(const SmartReport *r, char *out, size_t n);
/* One ATA attribute as JSON: {id, value, worst, thresh, raw, prefail, failing}. */
int  smart_attr_json(const SmartAttr *a, char *out, size_t n);

/* ---- helper actions (disk_admin.c) ---- */

void disk_act_smart(const AdminReq *r, const char *arg, size_t len);
void disk_act_mount(const AdminReq *r, const char *arg, size_t len);
void disk_act_unmount(const AdminReq *r, const char *arg, size_t len);
void disk_act_eject(const AdminReq *r, const char *arg, size_t len);
void disk_act_format(const AdminReq *r, const char *arg, size_t len);
void disk_act_label(const AdminReq *r, const char *arg, size_t len);
void disk_act_erase(const AdminReq *r, const char *arg, size_t len);
void disk_act_write_image(const AdminReq *r, const char *arg, size_t len);
void disk_act_cancel(const AdminReq *r, const char *arg, size_t len);

/* All are A_ANYONE: each one requires an administrator for internal disks (removable ones are
 * open to any session user, like udisks) and refuses the system disk. */
#define DISK_ADMIN_ACTIONS \
    { "disk-smart", disk_act_smart, A_FORK | A_ANYONE }, \
    { "disk-mount", disk_act_mount, A_ANYONE }, \
    { "disk-unmount", disk_act_unmount, A_ANYONE }, \
    { "disk-eject", disk_act_eject, A_FORK | A_ANYONE }, \
    { "disk-format", disk_act_format, A_FORK | A_ANYONE }, \
    { "disk-label", disk_act_label, A_FORK | A_ANYONE }, \
    { "disk-erase", disk_act_erase, A_FORK | A_ANYONE }, \
    { "disk-write-image", disk_act_write_image, A_FORK | A_ANYONE }, \
    { "disk-cancel", disk_act_cancel, A_ANYONE },

#endif
