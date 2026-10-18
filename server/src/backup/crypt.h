/* crypt.h — cifrado opcional de las Copias de seguridad, con Monocypher (vendorizado en
 * third_party/monocypher; ver docs/SISTEMA.md, «Cifrado»).
 *
 * Cada archivo de la copia es:
 *   "LBK1" | nonce base (24 bytes) | trozos de 64 KiB cifrados con XChaCha20-Poly1305 (+16 de MAC)
 * El nonce de cada trozo es el base con su número mezclado en los últimos 8 bytes, y los datos
 * asociados son ese número y si es el último trozo: no se pueden reordenar, quitar ni cortar
 * trozos sin que falle la verificación. Un archivo vacío es un solo trozo final vacío.
 * La clave sale de la frase con Argon2id (64 MiB, 3 pasadas) y una sal por destino. Los nombres de
 * los archivos y los destinos de los enlaces simbólicos NO se cifran. */
#ifndef LORD_BACKUP_CRYPT_H
#define LORD_BACKUP_CRYPT_H

#include <stddef.h>
#include <stdint.h>

#define BKC_CHUNK 65536u
#define BKC_HDR 28u
#define BKC_MAC 16u
#define BKC_KEY 32u
#define BKC_SALT 16u
#define BKC_FILE "cifrado.json"         /* en la carpeta del equipo: sal y verificador */

uint64_t bkc_enc_size(uint64_t plain);
int  bkc_plain_size(uint64_t enc, uint64_t *plain);   /* -1 si ese tamaño no puede ser de un cifrado */

/* tick(ud, bytes) entre trozos: != 0 corta. Devuelven 0 bien, -1 E/S, -2 sin lugar, 1 cortado;
 * descifrar además -3 si un trozo no verifica (archivo alterado o clave incorrecta). */
int  bkc_encrypt_fd(int in, int out, const uint8_t key[BKC_KEY], int (*tick)(void *ud, size_t n), void *ud);
int  bkc_decrypt_fd(int in, int out, const uint8_t key[BKC_KEY], int (*tick)(void *ud, size_t n), void *ud);

int  bkc_derive(const char *pass, const uint8_t salt[BKC_SALT], uint8_t key[BKC_KEY]);   /* lento: ~0,3 s */
void bkc_check(const uint8_t key[BKC_KEY], uint8_t out[16]);    /* verificador de la clave (no la revela) */
int  bkc_random(uint8_t *buf, size_t n);

/* <machine>/cifrado.json: crear (sal nueva + verificador) o leer. 0 bien. */
int  bkc_info_write(const char *machine, const uint8_t salt[BKC_SALT], const uint8_t check[16]);
int  bkc_info_read(const char *machine, uint8_t salt[BKC_SALT], uint8_t check[16]);   /* -1: no está cifrado */
/* Deriva y verifica la frase contra cifrado.json: 0 bien (key lleno), -1 frase incorrecta, -2 sin archivo. */
int  bkc_unlock(const char *machine, const char *pass, uint8_t key[BKC_KEY]);
/* Crea el cifrado de un destino nuevo: 0 bien. */
int  bkc_setup(const char *machine, const char *pass, uint8_t key[BKC_KEY]);
void bkc_wipe(void *p, size_t n);

#endif
