/* status.h — paquetes instalados, leídos de /var/lib/dpkg/status (solo lectura: la base de
 * dpkg la escribe únicamente dpkg). */
#ifndef LORD_PKG_STATUS_H
#define LORD_PKG_STATUS_H

#include <stddef.h>
#include <stdint.h>

#define DPKG_STATUS "/var/lib/dpkg/status"

typedef struct {
    const char *name, *version, *arch, *summary, *section, *source;
    const char *depends, *predepends, *provides, *conflicts, *breaks, *replaces, *recommends;
    uint32_t isize;
    uint8_t flags;                      /* IF_ESSENTIAL, IF_PROTECTED, IF_MA_* de idx.h */
    uint8_t prio;
    uint8_t native;                     /* arquitectura nativa o all */
    uint8_t config_only;                /* quitado pero con su configuración ("config-files") */
} StPkg;

typedef struct {
    StPkg *p;                           /* ordenados por nombre (los nativos primero) */
    int n;
    char *buf;
    long mtime, size;
} Status;

/* Carga (o recarga si cambió). arch: la nativa ("amd64"). Solo cuenta lo que tiene sus archivos
 * en el sistema (installed, unpacked, half-configured, triggers-*). 0 bien. */
int  status_load(Status *s, const char *path, const char *arch);
void status_free(Status *s);
/* Paquete instalado nativo (o all) con ese nombre, o NULL. */
const StPkg *status_find(const Status *s, const char *name, size_t n);

/* Arquitectura nativa de dpkg (amd64, arm64…), según la máquina. */
const char *pkg_native_arch(void);

#endif
