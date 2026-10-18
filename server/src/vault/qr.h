/* qr.h — códigos QR propios: generar (el QR de una red Wi-Fi para unirse desde el teléfono, la
 * clave de recuperación) y leer (agregar un código 2FA desde la imagen de un QR otpauth://).
 *
 * Generar: modos byte/alfanumérico/numérico, versiones 1–40, corrección L/M/Q/H, las 8 máscaras
 * con la penalización de ISO/IEC 18004. Leer: imágenes limpias (capturas de pantalla, PNG/JPEG
 * exportados, fotos derechas y nítidas): binariza, encuentra los tres patrones de posición, arma
 * la grilla (con el patrón de alineación desde la versión 2), lee el formato, saca la máscara y
 * corrige errores con Reed-Solomon. No es un lector de cámara (sin perspectiva fuerte). */
#ifndef LORD_VAULT_QR_H
#define LORD_VAULT_QR_H

#include <stddef.h>
#include <stdint.h>

enum { QR_ECC_L = 0, QR_ECC_M, QR_ECC_Q, QR_ECC_H };
#define QR_MAX_SIZE 177                 /* lado de la versión 40 */

/* Genera el QR de len bytes con la corrección pedida (sube de versión hasta que entre).
 * modules: QR_MAX_SIZE*QR_MAX_SIZE bytes (1 = oscuro), fila por fila con lado *size.
 * Devuelve la versión (1–40) o -1 si no entra. */
int qr_encode(const uint8_t *data, size_t len, int ecc, uint8_t *modules, int *size);

/* Lee un QR de una imagen 0xAARRGGBB (w*h). out recibe los bytes (con un 0 al final si entra).
 * Devuelve el largo o -1 si no se encontró/no se pudo leer. */
int qr_decode(const uint32_t *argb, int w, int h, uint8_t *out, size_t outcap);

/* Igual que qr_encode con máscara fija (0–7; -1 = la mejor) y versión mínima; *mask_out la usada. */
int qr_encode_ex(const uint8_t *data, size_t len, int ecc, int mask, int minver, uint8_t *modules, int *size, int *mask_out);

#endif
