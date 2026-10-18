/* vstb.h — stb_image (vendorizado) adentro de lord-vault, con la memoria en la arena de secretos
 * (smem.h): un KDBX descomprimido o la foto de un código QR nunca pasan por el montón de la libc.
 * Los símbolos de stb quedan estáticos (no chocan con los de ui/stb.c). */
#ifndef LORD_VAULT_VSTB_H
#define LORD_VAULT_VSTB_H

#include <stddef.h>
#include <stdint.h>

/* deflate crudo (sin cabecera zlib) → bytes en smem (liberar con sm_free); NULL si es inválido o
 * pasa de max bytes. */
uint8_t *vstb_inflate_raw(const uint8_t *in, size_t len, size_t max, size_t *outlen);
/* gzip (RFC 1952, como el de KDBX) → bytes en smem; NULL si es inválido. */
uint8_t *vstb_gunzip(const uint8_t *in, size_t len, size_t max, size_t *outlen);
/* PNG o JPEG → píxeles 0xAARRGGBB en smem (w*h); NULL si no se puede (lado máximo 8192). */
uint32_t *vstb_decode_argb(const uint8_t *in, size_t len, int *w, int *h);

#endif
