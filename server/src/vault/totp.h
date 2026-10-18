/* totp.h — códigos de un solo uso (2FA): HOTP (RFC 4226) y TOTP (RFC 6238) con SHA-1, SHA-256 y
 * SHA-512, base32 (RFC 4648) y URIs otpauth:// (el formato de Google Authenticator que traen los
 * QR de los sitios). El secreto nunca sale de lord-vault: la interfaz recibe solo el código. */
#ifndef LORD_VAULT_TOTP_H
#define LORD_VAULT_TOTP_H

#include <stddef.h>
#include <stdint.h>

enum { TOTP_SHA1 = 0, TOTP_SHA256, TOTP_SHA512 };

typedef struct {
    uint8_t key[128]; size_t keylen;
    int algo;                 /* TOTP_* */
    int digits;               /* 6–10 */
    int period;               /* segundos (30) */
    int hotp;                 /* 1 = otpauth://hotp (contador) */
    uint64_t counter;
    char issuer[128], account[128];
} Totp;

/* base32 → bytes (ignora espacios, guiones y '='; mayúsculas o minúsculas). Largo o -1. */
long base32_decode(const char *s, uint8_t *out, size_t cap);
size_t base32_encode(const uint8_t *in, size_t n, char *out, size_t cap);   /* sin '=' */

/* Lee un otpauth://totp/… o un secreto base32 suelto. 0 bien. */
int  totp_parse(const char *text, Totp *t);
uint32_t hotp_code(const Totp *t, uint64_t counter);
/* Código para la hora unix `now`; *remaining = segundos que le quedan. */
uint32_t totp_code(const Totp *t, uint64_t now, int *remaining);
/* Código con los ceros de adelante: out de al menos 11 bytes. */
void totp_format(const Totp *t, uint32_t code, char *out);
void totp_wipe(Totp *t);

#endif
