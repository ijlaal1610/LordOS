/* vcrypto.h — primitivas propias que Monocypher no trae y que piden los formatos ajenos:
 *   - AES-128/256 (bloque, CBC): CTAP2 (protocolo de PIN 1), KDBX (AES-256-CBC y AES-KDF) y el
 *     cifrado «dh-ietf1024-sha256-aes128-cbc-pkcs7» de Secret Service;
 *   - SHA-1 y HMAC-SHA1 (TOTP, RFC 6238/4226), HMAC-SHA256 (CTAP2, KDBX, TOTP-SHA256),
 *     HMAC-SHA512 (TOTP-SHA512) y HKDF-SHA256 (RFC 5869).
 * La bóveda propia usa solo Monocypher (XChaCha20-Poly1305, Argon2id, BLAKE2b).
 * SHA-256/512 son los de server/src/pkg/crypto.c. */
#ifndef LORD_VAULT_VCRYPTO_H
#define LORD_VAULT_VCRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* ---- AES ---- */
typedef struct { uint32_t rk[60]; int nr; } Aes;          /* claves de ronda (cifrar y descifrar) */
int  aes_init(Aes *a, const uint8_t *key, int keybits);   /* 128, 192 o 256; 0 bien */
void aes_encrypt(const Aes *a, const uint8_t in[16], uint8_t out[16]);
void aes_decrypt(const Aes *a, const uint8_t in[16], uint8_t out[16]);
void aes_wipe(Aes *a);
/* CBC sin relleno: len múltiplo de 16 (in y out pueden ser el mismo búfer). iv se actualiza. */
int  aes_cbc_encrypt(const Aes *a, uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);
int  aes_cbc_decrypt(const Aes *a, uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);
/* Quita el relleno PKCS#7 de un bloque ya descifrado: largo sin relleno o -1 si es inválido. */
long pkcs7_unpad(const uint8_t *buf, size_t len);

/* ---- SHA-1 ---- */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } Sha1;
void sha1_init(Sha1 *s);
void sha1_update(Sha1 *s, const void *data, size_t len);
void sha1_final(Sha1 *s, uint8_t out[20]);
void sha1(const void *data, size_t len, uint8_t out[20]);

/* ---- HMAC ---- */
void hmac_sha1(const uint8_t *key, size_t klen, const uint8_t *msg, size_t len, uint8_t out[20]);
void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t len, uint8_t out[32]);
/* HMAC-SHA256 de varias partes (KDBX: índice || largo || bloque). parts/lens terminan con NULL. */
void hmac_sha256_v(const uint8_t *key, size_t klen, const uint8_t *const *parts, const size_t *lens, uint8_t out[32]);
void hmac_sha512(const uint8_t *key, size_t klen, const uint8_t *msg, size_t len, uint8_t out[64]);
/* HKDF-SHA256 (RFC 5869); out de hasta 255*32 bytes. 0 bien. */
int  hkdf_sha256(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen,
                 const uint8_t *info, size_t infolen, uint8_t *out, size_t outlen);

/* ---- varios ---- */
int  vc_random(void *buf, size_t n);                       /* getrandom; 0 bien */
int  vc_equal(const void *a, const void *b, size_t n);     /* 1 si iguales (tiempo constante) */

#endif
