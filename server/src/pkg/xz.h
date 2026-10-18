/* xz.h — descompresión en memoria de .xz (LZMA2, propio) y .gz (inflate de stb_image). */
#ifndef LORD_PKG_XZ_H
#define LORD_PKG_XZ_H

#include <stddef.h>
#include <stdint.h>

/* 0 bien; *out con malloc. max: tamaño máximo de salida aceptado. */
int xz_decode(const uint8_t *in, size_t len, uint8_t **out, size_t *outlen, size_t max);
int gz_decode(const uint8_t *in, size_t len, uint8_t **out, size_t *outlen, size_t max);

#endif
