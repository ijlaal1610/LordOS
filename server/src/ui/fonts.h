/* fonts.h — catálogo de fuentes instaladas (TTF/OTF/TTC) para elegir la de la terminal.
 *
 * Busca en assets/fonts de LordOS, /usr/share/fonts, /usr/local/share/fonts,
 * ~/.local/share/fonts y ~/.fonts. Solo lee las tablas name/OS-2/post/hmtx con un parser
 * propio que valida límites; stb_truetype carga únicamente la fuente que se elige. */
#ifndef LORD_FONTS_H
#define LORD_FONTS_H

typedef struct {
    char family[64], style[48], ps[64];
    char path[512];
    int index;                  /* dentro de un .ttc */
    int weight, italic, mono, color;
} FontEntry;

void fonts_set_builtin_dir(const char *dir);
/* Escanea (una vez; force = volver a escanear) y devuelve el catálogo. */
const FontEntry *fonts_list(int force, int *count);
/* Mejor variante de la familia para negrita/itálica; NULL si no existe. */
const FontEntry *fonts_pick(const char *family, int bold, int italic);
/* Por nombre PostScript (como guarda iTerm2: "JetBrainsMono-Regular"). */
const FontEntry *fonts_by_ps(const char *ps);
/* Primera fuente a color instalada (Noto Color Emoji...). */
const FontEntry *fonts_color(void);

#endif
