/* disk.h — discos y particiones para la app Discos (ver docs/SISTEMA.md, «Discos»).
 *
 * disk.c lee sin root: /sys/block, la base de udev (/run/udev/data/b<mayor>:<menor>) y
 * /proc/self/mountinfo. Lo usan lord-server (lord.disks, js_disk.c) y el ayudante de
 * administración (disk_admin.c), que valida con estas mismas funciones del lado root.
 * smart.c son los analizadores (funciones puras) de SMART de ATA y del registro de salud de
 * NVMe; los lee el ayudante con SG_IO / NVME_IOCTL_ADMIN_CMD (acción disk-smart). */
#ifndef LORD_DISK_H
#define LORD_DISK_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../admin.h"

/* Nombre de un dispositivo de bloque como lo da el kernel: sda, sda1, nvme0n1p2, mmcblk0p1,
 * vdb, loop3 (letras minúsculas y dígitos, empieza con letra, hasta 31). */
int  disk_valid_name(const char *name);
/* 1 si es una partición (existe /sys/class/block/<n>/partition); su disco en disk. */
int  disk_parent(const char *name, char *disk, size_t n);
/* 1 si el disco (entero) aloja algo del sistema: /, /usr, /var, /home, /boot, /boot/efi, el medio
 * del live, una swap activa, o un dm/md que esté sobre él y aloje alguno de esos. */
int  disk_is_system(const char *disk);
/* 1 si es extraíble: removable=1 en sysfs, o conectado por USB, o una tarjeta (mmc). */
int  disk_is_removable(const char *disk);
/* Propiedad de la base de udev para ese dispositivo ("ID_FS_TYPE"…); "" si no está. */
int  disk_udev_prop(const char *name, const char *key, char *out, size_t n);
/* Etiqueta del sistema de archivos tal cual (ID_FS_LABEL_ENC sin los \xNN); "" si no tiene. */
void disk_fs_label(const char *name, char *out, size_t n);
/* Tamaño en bytes (/sys/class/block/<n>/size × 512). */
uint64_t disk_size(const char *name);
/* Puntos de montaje del dispositivo (separados por \n) desde mountinfo; devuelve cuántos. */
int  disk_mounts(const char *name, char *out, size_t n);

/* Lista para JS: [{ name, model, vendor, serial, size, removable, transport, rotational, ro,
 * system, table, fstype, label, uuid, mountpoint, used, avail, partitions: [{ name, number, size,
 * fstype, label, uuid, partlabel, mountpoint, used, avail, start }] }]. malloc; NULL si falla. */
char *disk_list_json(void);
/* Programas para formatear y renombrar que hay en el equipo: {"fat32":1,"exfat":1,"ext4":1,"ntfs":0} */
char *disk_tools_json(void);

/* Avisos de cambios para la app abierta: un fd (epoll de mountinfo y de los avisos de udev) para
 * el bucle principal. disk_watch(1) lo abre, disk_watch(0) lo cierra; -1 si no está abierto. */
int  disk_watch(int on);
int  disk_watch_fd(void);
/* Lee lo que llegó; 1 si cambió algo de discos o montajes (hay que avisar a JS). */
int  disk_watch_dispatch(void);

/* ---- SMART (smart.c, funciones puras: tests/smart_test.c) ---- */

enum { SMART_UNKNOWN = 0, SMART_OK = 1, SMART_WARN = 2, SMART_FAIL = 3 };
enum { SMART_ATA = 1, SMART_NVME = 2 };

typedef struct {
    uint8_t id, value, worst, thresh;
    uint16_t flags;                     /* bit 0: pre-falla */
    uint64_t raw;                       /* 48 bits */
    int failing;                        /* valor <= umbral (umbral != 0) */
} SmartAttr;

typedef struct {
    int kind;                           /* SMART_ATA / SMART_NVME */
    int health;                         /* SMART_OK… */
    int checksum_ok;                    /* ATA: la suma de los 512 bytes da 0 */
    int status;                         /* ATA RETURN STATUS: 1 bien, 0 umbral superado, -1 no se sabe */
    int temp_c;                         /* -1 si no se sabe */
    int64_t power_on_hours, power_cycles;           /* -1 si no se sabe */
    int64_t reallocated, pending, uncorrectable;    /* ATA 5, 197, 198 (-1 si no están) */
    int nattr;
    SmartAttr attr[30];
    /* NVMe */
    int crit_warning, spare, spare_thresh, used_pct;
    uint64_t data_read, data_written, media_errors, unsafe_shutdowns;   /* data_*: bytes */
} SmartReport;

/* data = SMART READ DATA (512 bytes); thresh = READ THRESHOLDS (512) o NULL; status = 1/0/-1. */
int  smart_ata_parse(const uint8_t data[512], const uint8_t *thresh, int status, SmartReport *r);
/* log = registro 02h de NVMe (SMART / Health Information, 512 bytes). */
int  smart_nvme_parse(const uint8_t log[512], SmartReport *r);
/* Resumen en JSON (sin los atributos): {kind, health, temp, hours, cycles, ...}. */
int  smart_summary_json(const SmartReport *r, char *out, size_t n);
/* Un atributo ATA en JSON: {id, value, worst, thresh, raw, prefail, failing}. */
int  smart_attr_json(const SmartAttr *a, char *out, size_t n);

/* ---- acciones del ayudante (disk_admin.c) ---- */

void disk_act_smart(const AdminReq *r, const char *arg, size_t len);
void disk_act_mount(const AdminReq *r, const char *arg, size_t len);
void disk_act_unmount(const AdminReq *r, const char *arg, size_t len);
void disk_act_eject(const AdminReq *r, const char *arg, size_t len);
void disk_act_format(const AdminReq *r, const char *arg, size_t len);
void disk_act_label(const AdminReq *r, const char *arg, size_t len);
void disk_act_erase(const AdminReq *r, const char *arg, size_t len);
void disk_act_write_image(const AdminReq *r, const char *arg, size_t len);
void disk_act_cancel(const AdminReq *r, const char *arg, size_t len);

/* Todas son A_ANYONE: cada una exige adentro administrador para los discos internos (los
 * extraíbles los puede usar cualquiera, como udisks) y rechaza el disco del sistema. */
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
