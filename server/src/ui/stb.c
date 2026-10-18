/* stb.c — implementación de las librerías stb vendorizadas (se compila sin warnings). */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
/* en ARM64 (Raspberry Pi) los bucles NEON de stb para JPEG; en x86-64 stb ya usa SSE2 solo */
#if defined(__aarch64__) && !defined(STBI_NEON) && !defined(STBI_NO_SIMD)
#define STBI_NEON
#endif
#include "stb_image.h"
