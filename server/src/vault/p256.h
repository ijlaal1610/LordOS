/* p256.h — curva NIST P-256 propia, solo lo que pide CTAP2: el acuerdo de claves ECDH del
 * protocolo de PIN 1 y de la extensión hmac-secret (COSE alg -25, ECDH-ES+HKDF-256).
 *
 * Aritmética de Montgomery con palabras de 32 bits (para p y n), coordenadas proyectivas con las
 * fórmulas COMPLETAS de Renes–Costello–Batina (2016, alg. 4, a = -3: sin casos especiales para
 * el infinito ni para P = Q) y escalera de Montgomery con intercambio condicional sin ramas.
 * No hay ECDSA: nuestro cliente nunca verifica firmas (la clave que sale de hmac-secret se
 * comprueba sola al descifrar la bóveda). Escalares y coordenadas en big-endian de 32 bytes. */
#ifndef LORD_VAULT_P256_H
#define LORD_VAULT_P256_H

#include <stdint.h>

/* Par de claves al azar (getrandom). 0 bien. */
int p256_keypair(uint8_t priv[32], uint8_t x[32], uint8_t y[32]);
/* Clave pública de priv (1 <= priv < n). 0 bien, -1 si priv no es válida. */
int p256_public(const uint8_t priv[32], uint8_t x[32], uint8_t y[32]);
/* x de priv * (px, py). -1 si el punto no está en la curva o el resultado es el infinito. */
int p256_ecdh(const uint8_t priv[32], const uint8_t px[32], const uint8_t py[32], uint8_t shared_x[32]);
/* 1 si (x, y) es un punto de la curva (con x, y < p). */
int p256_on_curve(const uint8_t x[32], const uint8_t y[32]);
/* Para las pruebas: k * G en afín; -1 si da el infinito (k = 0 o múltiplo de n). k puede ser
 * cualquier número de 256 bits (no se exige k < n). */
int p256_mul_base(const uint8_t k[32], uint8_t x[32], uint8_t y[32]);

#endif
