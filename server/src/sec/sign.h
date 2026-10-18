/* sign.h — firma de los paquetes .lpk y el catálogo firmado (ver docs/FIRMA.md).
 *
 * Un .lpk firmado trae, además de sus archivos:
 *   contenido.json   { format: 1, id, version, signer: { name, key }, files: [{ path, size, sha256 }] }
 *   firma.json       { format: 1, alg: "ed25519", key, sig }   sig = Ed25519 de los bytes de contenido.json
 *
 * El catálogo (catalog.json + catalog.sig, firmado con una clave oficial) trae las versiones, los
 * tamaños y hashes de los paquetes, las claves de autores verificados y la lista de revocados. */
#ifndef LORD_SEC_SIGN_H
#define LORD_SEC_SIGN_H

#include "../lpk.h"
#include <stddef.h>
#include <stdint.h>

#define SIG_CONTENTS "contenido.json"
#define SIG_SIGNATURE "firma.json"

typedef enum {
    LPK_UNSIGNED = 0,                   /* «Sin firma» */
    LPK_SIGNED = 1,                     /* «Firmada por <nombre>» (clave desconocida) */
    LPK_VERIFIED = 2,                   /* «Autor verificado» (clave del catálogo) */
    LPK_OFFICIAL = 3,                   /* «LordOS» (clave oficial) */
} LpkTrust;

typedef struct {
    int has_contents, has_signature;
    LpkTrust trust;
    char key[65];                       /* clave pública en hex ("" sin firma) */
    char signer[128];                   /* nombre: del catálogo si está verificado, si no el del paquete */
    char contents_sha[65];              /* SHA-256 de contenido.json ("" si no hay) */
    int revoked;
    char revoked_why[160];
} LpkSigInfo;

/* Verifica contenido.json y firma.json contra los archivos de pk (id y versión del manifest).
 * 0 = bien (también un paquete sin firma ni contenido.json); -1 = el paquete fue alterado, está
 * mal firmado o le faltan archivos (err dice por qué). */
int sig_verify_package(const Lpk *pk, const char *id, const char *version, LpkSigInfo *out, char *err, size_t errlen);

/* Verifica una firma.json (alg, key, sig) sobre data. 0 bien; key (65) recibe la clave. */
int sig_verify_detached(const char *sig_json, size_t siglen, const void *data, size_t len, char key[65], char *err, size_t errlen);

/* ¿Es una clave oficial de LordOS? (keys.h) */
int sig_official(const char *keyhex);

/* Texto de firma.json para data con la semilla seed (lo usa «Compartir mi versión»). */
int sig_make_detached(const uint8_t seed[32], const void *data, size_t len, char *out, size_t outlen);

/* ---- catálogo ---- */

typedef struct { char key[65], name[96]; } CatAuthor;
typedef struct { char id[48], version[40], sha[65]; } CatRevoked;
typedef struct { char id[48], version[40], sha256[65], key[65]; double size; } CatPackage;

typedef struct {
    int ok;
    double serial;
    char date[32];
    char path[512];
    CatAuthor *authors;      int nauthors;
    char (*revoked_keys)[65]; int nrevoked_keys;
    CatRevoked *revoked;     int nrevoked;
    CatPackage *packages;    int npackages;
} Catalog;

/* Lee catalog.json con su catalog.sig (firmado por una clave oficial). 0 bien. */
int  catalog_parse(const char *json, size_t len, const char *sig, size_t siglen, Catalog *out, char *err, size_t errlen);
void catalog_free(Catalog *c);

/* El catálogo vigente: el válido de serial más alto entre /usr/share/lordos/catalog.json y
 * ~/.local/share/lordos/catalog/catalog.json. Se relee si cambió el archivo. NULL si no hay. */
const Catalog *catalog_current(void);
const char *catalog_author(const char *keyhex);
/* ¿Revocado? key, id, versión y SHA-256 de contenido.json (cualquiera puede ser ""). */
int catalog_revoked(const char *key, const char *id, const char *version, const char *contents_sha, char *why, size_t n);

/* Nombre del nivel para los registros: "lordos", "verified", "signed", "unsigned". */
const char *sig_trust_name(LpkTrust t);

#endif
