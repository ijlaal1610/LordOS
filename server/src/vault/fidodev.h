/* fidodev.h — encontrar y abrir llaves FIDO2 (lado de lord-server, sin caja de arena).
 *
 * Recorre /sys/class/hidraw/hidraw*: un dispositivo es FIDO si su descriptor de reportes HID
 * (device/report_descriptor) declara una colección de aplicación con Usage Page 0xF1D0 (FIDO
 * Alliance) y Usage 0x01 (CTAPHID). El nombre y vid:pid salen de device/uevent (HID_NAME,
 * HID_ID). lord-server abre el elegido (después del permiso del usuario) y le pasa el fd a
 * lord-vault; ni la app ni lord-vault abren /dev. */
#ifndef LORD_VAULT_FIDODEV_H
#define LORD_VAULT_FIDODEV_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char path[32];          /* "/dev/hidraw3" */
    char name[128];         /* "Yubico YubiKey OTP+FIDO+CCID" */
    unsigned vid, pid;
} FidoDev;

/* Hasta max llaves; devuelve cuántas. */
int fidodev_list(FidoDev *out, int max);
/* Abre path (tiene que ser /dev/hidrawN y una llave FIDO) con O_RDWR|O_CLOEXEC|O_NONBLOCK.
 * fd o -1 (errno). */
int fidodev_open(const char *path);
/* 1 si el descriptor HID declara la colección FIDO (para pruebas y para fidodev_list). */
int fidodev_desc_is_fido(const uint8_t *desc, size_t len);
/* Para pruebas: otra raíz en vez de "/sys/class/hidraw" (NULL vuelve a la de siempre). */
void fidodev_set_sysroot(const char *dir);

#endif
