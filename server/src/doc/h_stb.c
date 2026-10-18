/* h_stb.c — stb_image y stb_image_write para el ayudante (las imágenes que llegan de afuera,
 * como las del escáner, se decodifican acá y no en lord-server). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
