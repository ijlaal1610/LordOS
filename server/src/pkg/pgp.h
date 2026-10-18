/* pgp.h — lo que hace falta de OpenPGP (RFC 4880) para verificar el InRelease de un repositorio:
 * llaveros binarios o armados (.gpg/.pgp/.asc), firmas v4 RSA o EdDSA (Ed25519) con SHA-256 o
 * SHA-512, y mensajes firmados en claro. Código propio (sin gpgv ni sqv). */
#ifndef LORD_PKG_PGP_H
#define LORD_PKG_PGP_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum { PGP_RSA = 1, PGP_EDDSA = 22 };

typedef struct {
    uint8_t fpr[20];                    /* huella v4 (SHA-1 del paquete de la clave) */
    int alg;
    const uint8_t *n, *e;               /* RSA, apuntan al buffer del llavero */
    size_t nlen, elen;
    uint8_t ed[32];                     /* EdDSA */
    int64_t created, expires;           /* expires 0: nunca */
    int revoked, can_sign, primary;
} PgpKey;

typedef struct {
    PgpKey *keys;
    int n, cap;
    uint8_t **bufs;                     /* datos de cada llavero cargado */
    int nbufs;
} PgpKeyring;

int  pgp_keyring_add_file(PgpKeyring *kr, const char *path);      /* 0 bien */
int  pgp_keyring_add_mem(PgpKeyring *kr, const char *data, size_t len);   /* binario o armado */
void pgp_keyring_free(PgpKeyring *kr);

/* Verifica un mensaje firmado en claro (InRelease). Devuelve la cantidad de firmas válidas de
 * claves del llavero (> 0 = bien; 0 o menos = mal, con el motivo en err). Una firma inválida de
 * una clave conocida hace fallar todo. *body recibe el texto firmado (con \n, malloc). */
int  pgp_verify_clearsigned(const PgpKeyring *kr, const char *msg, size_t len, int64_t now,
                            char **body, size_t *body_len, char *err, size_t errlen);

/* Base64 de la armadura; devuelve bytes escritos o -1. */
long pgp_base64_decode(const char *in, size_t len, uint8_t *out, size_t cap);

#endif
