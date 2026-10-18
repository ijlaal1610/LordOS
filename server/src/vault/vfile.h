/* vfile.h — el formato de archivo de la bóveda (.lvault), versión 1. Ver docs/PASSWORDS.md,
 * «El formato».
 *
 *   cabecera   "LORDVLT\x1a", versión, largo de cabecera, id de la bóveda, fechas, contador,
 *              y las «ranuras» (una por forma de abrir: contraseña, archivo llave, contraseña +
 *              archivo llave, llave FIDO2, clave de recuperación, contraseña de la sesión)
 *   cuerpo     nonce(24) largo(8) XChaCha20-Poly1305(contenido JSON) mac(16)
 *              AD = la cabecera entera: cambiar una ranura o un parámetro se detecta al abrir
 *   adjuntos   cantidad(4) y por cada uno: id(16) nonce(24) largo(8) cifrado mac(16)
 *   cola       "LVLTEND\0" + largo total(8): un archivo cortado se reconoce
 *
 * La clave de la bóveda (VK, 32 bytes al azar) nunca se deriva de la contraseña: cada ranura la
 * guarda envuelta (XChaCha20-Poly1305) con una clave derivada de su factor (Argon2id para lo que
 * escribe una persona, BLAKE2b para lo que ya tiene entropía: archivo llave, salida de la llave
 * FIDO2, clave de recuperación). Así se agregan o quitan formas de abrir sin volver a cifrar
 * todo, y cambiar la contraseña es reescribir una ranura.
 *
 * Todo en little endian. */
#ifndef LORD_VAULT_VFILE_H
#define LORD_VAULT_VFILE_H

#include <stddef.h>
#include <stdint.h>

#define VF_MAGIC "LORDVLT\x1a"
#define VF_VERSION 1
#define VF_MAX_SLOTS 16
#define VF_HDR_FIXED 56
#define VF_MAX_BODY (256u << 20)
#define VF_MAX_ATT  (64u << 20)

enum { VS_PASSWORD = 1, VS_KEYFILE, VS_PASSKEY, VS_FIDO2, VS_RECOVERY, VS_SESSION };
enum { VK_ARGON2ID = 1, VK_BLAKE2B = 2 };
enum {
    VF_OK = 0, VF_EIO = -1, VF_EFORMAT = -2, VF_EVERSION = -3, VF_ETRUNC = -4,
    VF_EBADKEY = -5,       /* ningún factor abre una ranura (contraseña incorrecta, otra llave…) */
    VF_ECORRUPT = -6,      /* abrió la ranura pero el cuerpo no autentica: alterado */
    VF_ENOMEM = -7, VF_ENOSLOT = -8,
};

typedef struct { uint32_t m_kib, t_cost, lanes; } VKdf;

typedef struct {
    uint8_t type, kdf;
    uint8_t id[8];
    char label[64];
    VKdf p;                        /* VK_ARGON2ID */
    uint8_t salt[16];
    uint8_t cred[255]; uint8_t cred_len; uint8_t hmac_salt[32]; uint8_t aaguid[16];   /* VS_FIDO2 */
    uint8_t fido_pin;              /* VS_FIDO2: 1 si se usó el PIN de la llave (cambia la salida de hmac-secret) */
    uint64_t created;
    uint8_t nonce[24], wrapped[32], mac[16];
} VSlot;

typedef struct { uint8_t id[16]; uint64_t off, len; uint8_t nonce[24], mac[16]; } VAttLoc;  /* en el archivo */

typedef struct {
    uint8_t vault_id[16];
    uint64_t created, saved;
    uint32_t counter;
    uint16_t flags;
    VSlot slots[VF_MAX_SLOTS];
    int nslots;
    size_t hlen;                   /* bytes de cabecera (al leer) */
    /* al leer: dónde está el cuerpo y los adjuntos */
    uint64_t body_off, body_len;
    VAttLoc *atts; int natts;      /* malloc (no son secretos: ids y posiciones) */
} VHeader;

/* Factores que trae el usuario (los que no se usan, en NULL). */
typedef struct {
    const char *password;          /* VS_PASSWORD, VS_PASSKEY, VS_SESSION */
    const uint8_t *keyfile;        /* 32 bytes: BLAKE2b del archivo llave (vf_keyfile_hash) */
    const uint8_t *fido;           /* 32 bytes: salida hmac-secret de la llave */
    const char *recovery;          /* la clave de recuperación tal como se escribió */
} VFactors;

/* Lee la cabecera y la tabla del archivo (data = todo el archivo o al menos hasta la cola). */
int  vf_parse(const uint8_t *data, size_t len, VHeader *h);
void vf_header_free(VHeader *h);
/* Prueba las ranuras del tipo `type` (0 = cualquiera que use los factores dados) y deja la VK.
 * *slot = índice de la que abrió. VF_EBADKEY si ninguna. */
int  vf_open_slot(const VHeader *h, int type, const VFactors *f, uint8_t vk[32], int *slot);
/* Descifra el cuerpo con la VK → texto en smem (sm_free). VF_ECORRUPT si no autentica. */
int  vf_read_body(const uint8_t *data, size_t len, const VHeader *h, const uint8_t vk[32], uint8_t **out, size_t *outlen);
/* Arma una ranura nueva del tipo dado que envuelve la VK (deriva con los factores). */
int  vf_make_slot(const VHeader *h, VSlot *s, int type, const VFactors *f, const uint8_t vk[32], const VKdf *kdf, const char *label);
/* Adjuntos: cifrar (al agregarlo) y descifrar (al pedirlo). id/nonce/mac quedan en el contenido. */
int  vf_att_seal(const uint8_t vk[32], const uint8_t vault_id[16], const uint8_t id[16], const uint8_t *plain, size_t len,
                 uint8_t *cipher, uint8_t nonce[24], uint8_t mac[16]);
int  vf_att_open(const uint8_t vk[32], const uint8_t vault_id[16], const uint8_t id[16], const uint8_t *cipher, size_t len,
                 const uint8_t nonce[24], const uint8_t mac[16], uint8_t *plain);

/* Un adjunto para escribir: los bytes cifrados vienen de memoria o del archivo viejo (fd, off). */
typedef struct { uint8_t id[16], nonce[24], mac[16]; const uint8_t *cipher; int srcfd; uint64_t off, len; } VAttOut;
/* Guarda la bóveda en dirfd/name de forma atómica: escribe name.tmp-<pid>, fsync, deja la versión
 * anterior como name.bak (enlace duro, reemplazado atómicamente) y renombra. h->saved y h->counter
 * se actualizan. Devuelve VF_OK o VF_EIO. */
int  vf_save(int dirfd, const char *name, VHeader *h, const uint8_t vk[32], const uint8_t *plain, size_t plen,
             const VAttOut *atts, int natts);
/* Lo mismo pero a un fd ya abierto (exportar una copia): sin .bak ni renombrar. */
int  vf_write_fd(int fd, VHeader *h, const uint8_t vk[32], const uint8_t *plain, size_t plen, const VAttOut *atts, int natts);

/* Cabecera nueva (id al azar). */
void vf_header_new(VHeader *h);
/* BLAKE2b-256 de un archivo llave. */
void vf_keyfile_hash(const uint8_t *data, size_t len, uint8_t out[32]);
/* Clave de recuperación nueva: 40 caracteres base32 de Crockford en 8 grupos ("ABCDE-FGHJK-…"). */
void vf_recovery_new(char out[48]);
/* Parámetros de Argon2id para tardar unos target_ms en esta máquina (mide). */
void vf_calibrate(int target_ms, VKdf *out, int *measured_ms);
/* Deriva con Argon2id (memoria en smem). 0 bien. */
int  vf_argon2id(const uint8_t *pass, size_t plen, const uint8_t salt[16], const VKdf *p, uint8_t out[32]);
const char *vf_strerror(int err);

#endif
