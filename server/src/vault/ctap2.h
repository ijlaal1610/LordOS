/* ctap2.h — cliente FIDO2 propio (CTAP 2.0/2.1 sobre USB HID) para desbloquear la bóveda con una
 * llave de seguridad (ver docs/PASSWORDS.md, «Llave de seguridad FIDO2»).
 *
 * Usa la extensión `hmac-secret`: la llave guarda un secreto por credencial (CredRandom) y, dado
 * un «salt» de 32 bytes, devuelve HMAC-SHA256(CredRandom, salt). Ese resultado es la clave que
 * envuelve la clave de la bóveda: sin la llave física (y su toque) no se puede abrir.
 *
 * Corre en lord-vault, encerrado: recibe el fd de /dev/hidrawN ya abierto por lord-server
 * (después del permiso del usuario) y solo hace read/write de reportes de 64 bytes, sin ioctl.
 * Protocolo: CTAPHID (INIT con nonce, canal propio, CBOR, KEEPALIVE, CANCEL, ERROR) +
 * authenticatorGetInfo, authenticatorClientPIN (getKeyAgreement y getPinToken, protocolo de PIN 1:
 * ECDH P-256 + SHA-256 + AES-256-CBC), authenticatorMakeCredential y authenticatorGetAssertion
 * con hmac-secret. El acuerdo de claves usa P-256 propio (p256.h). */
#ifndef LORD_VAULT_CTAP2_H
#define LORD_VAULT_CTAP2_H

#include <stddef.h>
#include <stdint.h>

#define FIDO_RP_ID "contrasenas.lordos"      /* rp.id de las credenciales de la bóveda */
#define FIDO_CRED_MAX 255

enum {
    FIDO_OK = 0,
    FIDO_ERR_IO = -1,          /* el dispositivo no responde o se desenchufó */
    FIDO_ERR_TIMEOUT = -2,     /* nadie tocó la llave a tiempo */
    FIDO_ERR_PROTOCOL = -3,    /* respuesta inválida */
    FIDO_ERR_NO_HMAC = -4,     /* la llave no tiene hmac-secret (solo U2F, o muy vieja) */
    FIDO_ERR_PIN_REQUIRED = -5,/* la llave tiene PIN y no se pasó */
    FIDO_ERR_PIN_INVALID = -6,
    FIDO_ERR_PIN_BLOCKED = -7,
    FIDO_ERR_NO_CREDENTIAL = -8,/* la credencial no es de esta llave (otra llave) */
    FIDO_ERR_CANCELLED = -9,
    FIDO_ERR_DENIED = -10,     /* la llave rechazó (CTAP2_ERR_OPERATION_DENIED…) */
};

typedef struct {
    int hmac_secret;           /* anuncia la extensión */
    int client_pin;            /* -1 no soporta PIN, 0 soporta y no tiene, 1 tiene PIN */
    int uv;                    /* verificación propia (huella) */
    uint8_t aaguid[16];
    char versions[64];         /* "FIDO_2_0,FIDO_2_1,U2F_V2" */
    int pin_retries;           /* -1 si no se sabe */
} FidoInfo;

/* Avisos mientras se espera: FIDO_ST_TOUCH = la llave pide que la toquen (KEEPALIVE UPNEEDED). */
enum { FIDO_ST_PROCESSING = 1, FIDO_ST_TOUCH = 2 };
typedef void (*FidoStatusCb)(void *ud, int status);

/* Todas: fd de hidraw abierto en lectura y escritura; timeout_ms para esperar el toque. */
int  fido_get_info(int fd, FidoInfo *info);
/* Crea una credencial nueva con hmac-secret para la bóveda (pide un toque). cred recibe el id de
 * la credencial (hasta FIDO_CRED_MAX bytes). pin: NULL o el PIN de la llave (UTF-8). */
int  fido_make_credential(int fd, const char *pin, uint8_t *cred, size_t *cred_len,
                          int timeout_ms, FidoStatusCb cb, void *ud);
/* hmac-secret de esa credencial con salt (pide un toque): out = HMAC-SHA256(CredRandom, salt). */
int  fido_hmac_secret(int fd, const char *pin, const uint8_t *cred, size_t cred_len,
                      const uint8_t salt[32], uint8_t out[32], int timeout_ms, FidoStatusCb cb, void *ud);
/* Corta una espera en curso (desde otro hilo): manda CTAPHID_CANCEL. */
void fido_cancel(int fd);
const char *fido_strerror(int err);    /* código corto en inglés: "timeout", "pin-required"… */

/* ---- agregados ----
 * Notas de uso:
 *   - cred tiene que tener lugar para FIDO_CRED_MAX bytes (*cred_len es solo de salida).
 *   - hmac-secret con PIN (pin != NULL → pinAuth, UV) y sin PIN (solo toque) pueden dar
 *     resultados DISTINTOS en llaves CTAP 2.1 (CredRandomWithUV / WithoutUV): la bóveda tiene que
 *     guardar en la ranura si se usó PIN y repetir lo mismo al desbloquear.
 *   - Con PIN puesto en la llave, makeCredential exige el PIN (FIDO_ERR_PIN_REQUIRED). */

/* Pone un PIN nuevo en una llave que no tiene (clientPIN setPIN, protocolo 1; 4–63 bytes). */
int  fido_set_pin(int fd, const char *pin);
/* Transporte crudo: CTAPHID_INIT y un comando (cmd con el bit 0x80) con su respuesta. Para
 * pruebas y comandos del fabricante. Devuelve 0 y *resp_len, o un FIDO_ERR_*. */
int  ctaphid_command(int fd, uint8_t cmd, const uint8_t *data, size_t len,
                     uint8_t *resp, size_t cap, size_t *resp_len, int timeout_ms);

#endif
