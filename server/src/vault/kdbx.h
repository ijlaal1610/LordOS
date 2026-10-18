/* kdbx.h — lector propio de bases de KeePass 2.x / KeePassXC (.kdbx), para importar (ver
 * vimport.h y docs/CONTRASENAS.md, «Importar y exportar»).
 *
 * Versiones: KDBX 4.0/4.1 (cabecera con SHA-256 y HMAC, bloques HMAC-SHA256, encabezado interno
 * con los adjuntos) y KDBX 3.1 (bloques con SHA-256, adjuntos en Meta/Binaries). KDF: Argon2d,
 * Argon2id (versión 0x13, Monocypher) y AES-KDF; cifrados: AES-256-CBC y ChaCha20 (Twofish no:
 * "unsupported-cipher"); flujo interno de valores protegidos: ChaCha20 (4.x) y Salsa20 (3.1).
 * Clave compuesta: contraseña y/o archivo llave (XML 1.0/2.0, 32 bytes, 64 hex o SHA-256 del
 * archivo). Errores en ImpList.err, como imp_parse. */
#ifndef LORD_VAULT_KDBX_H
#define LORD_VAULT_KDBX_H

#include <stddef.h>
#include <stdint.h>
#include "vimport.h"

int kdbx_is(const uint8_t *d, size_t len);          /* 1 si empieza con la firma de KDBX 2.x+ */
/* password NULL o "" con archivo llave = sin contraseña en la clave compuesta. */
int kdbx_parse(const uint8_t *d, size_t len, const char *password, const uint8_t *keyfile, size_t kflen, ImpList *l);
/* Clave de 32 bytes de un archivo llave de KeePass. 0 bien, -1 si es un XML de llave inválido
 * (con Hash que no coincide). */
int kdbx_keyfile_key(const uint8_t *kf, size_t n, uint8_t out[32]);

/* Topes contra archivos que piden un KDF desmedido (se rechazan con "too-big").
 * Por defecto 500 millones de rondas de AES-KDF y 2 GiB de memoria para Argon2. */
extern uint64_t kdbx_max_aes_rounds;
extern uint64_t kdbx_max_argon_kib;

/* AES-KDF (transformar la clave compuesta): AES-NI si la CPU lo tiene, si no tablas T.
 * kdbx_aes_kdf_mode = 1 fuerza las tablas; kdbx_aes_kdf_ref usa el AES por bytes de vcrypto.c
 * (para comparar en las pruebas). */
extern int kdbx_aes_kdf_mode;
int kdbx_aes_kdf(const uint8_t comp[32], const uint8_t seed[32], uint64_t rounds, uint8_t out[32]);
int kdbx_aes_kdf_ref(const uint8_t comp[32], const uint8_t seed[32], uint64_t rounds, uint8_t out[32]);

#endif
