/* pkgstate.h — estado de las apps .lpk instaladas: el paquete original que se guarda al instalar,
 * «Modificada por vos» (comparando con los hashes del original), «Restaurar original»,
 * «Compartir mi versión» y la regla de las actualizaciones (misma clave). Ver docs/FIRMA.md. */
#ifndef LORD_SEC_PKGSTATE_H
#define LORD_SEC_PKGSTATE_H

#include "sign.h"
#include <stddef.h>
#include <stdint.h>

#define PKG_MAX_CHANGED 64

typedef struct {
    int installed, has_original, modified, nchanged;
    LpkSigInfo sig;                     /* del original guardado */
    char version[40];
    char changed[PKG_MAX_CHANGED][224];
    char change_kind[PKG_MAX_CHANGED];  /* 'm' modificado, 'a' agregado, 'r' borrado */
    char error[160];                    /* el original no verifica (no debería pasar) */
} PkgStatus;

/* ~/.local/share/lordos/apps/.orig/<id>.lpk */
int pkg_original_path(const char *id, char *out, size_t n);
/* Copia el .lpk recién instalado como original (atómico). */
int pkg_keep_original(const char *id, const char *lpk_path);
void pkg_forget(const char *id);        /* al desinstalar */

/* Estado de una app instalada (cacheado: se recalcula si cambió algún archivo). */
int pkg_status(const char *id, PkgStatus *st);

/* Regla de las actualizaciones (como Android): si la versión instalada está firmada, la nueva
 * tiene que estar firmada con la misma clave. 0 = se puede instalar. */
int pkg_check_update(const char *id, const LpkSigInfo *next, char *err, size_t n);

/* Vuelve a instalar exactamente el original. */
int pkg_restore(const char *id, char *err, size_t n);

/* Arma un .lpk nuevo con los archivos instalados (la versión del usuario) y el manifest dado
 * (con basedOn), firmado con la clave propia del usuario, en out_path (no pisa). */
int pkg_share(const char *id, const char *manifest, size_t mlen, const char *out_path, char *err, size_t n);

/* Clave propia del usuario (~/.local/share/lordos/keys/user.json, 0600): se crea la primera vez. */
int pkg_user_key(uint8_t seed[32], char pubhex[65], char name[128]);

#endif
