/* keyboard.h — distribuciones de teclado con libxkbcommon (ver keyboard.c). */
#ifndef LORD_KEYBOARD_H
#define LORD_KEYBOARD_H

#include <stdint.h>

typedef struct {
    char key[32];               /* "Enter", "ArrowUp", "a", "ñ"… (como KeyboardEvent.key) */
    char chr[16];               /* texto que escribe (con compose aplicado), "" si nada */
    char base[8];               /* carácter de la tecla sin Ctrl/Alt (para atajos) */
    int shift, ctrl, alt, altgr, meta, caps, dead;
} KeyInfo;

int  kb_set(const char *layouts, const char *variants);   /* "us,latam", ",abnt2" */
int  kb_ready(void);
const char *kb_layouts(void);
const char *kb_variants(void);
int  kb_group(void);                                      /* distribución activa */
void kb_set_group(int g);
void kb_reload_compose(void);                             /* después de cambiar la región */
void kb_key(int code, int value, KeyInfo *k);
/* Para Wayland: el mapa de teclado en formato XKB (malloc, NULL si no hay) y los
 * modificadores serializados. */
char *kb_keymap_string(void);
void kb_mods(uint32_t *depressed, uint32_t *latched, uint32_t *locked, uint32_t *group);

#endif
