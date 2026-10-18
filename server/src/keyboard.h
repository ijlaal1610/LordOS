/* keyboard.h — keyboard layouts with libxkbcommon (see keyboard.c). */
#ifndef LORD_KEYBOARD_H
#define LORD_KEYBOARD_H

#include <stdint.h>

typedef struct {
    char key[32];               /* "Enter", "ArrowUp", "a", "ñ"… (like KeyboardEvent.key) */
    char chr[16];               /* text it types (with compose applied), "" if nothing */
    char base[8];               /* character of the key without Ctrl/Alt (for shortcuts) */
    int shift, ctrl, alt, altgr, meta, caps, dead;
} KeyInfo;

int  kb_set(const char *layouts, const char *variants);   /* "us,latam", ",abnt2" */
int  kb_set_keymap_string(const char *text);              /* app mode: the compositor's keymap */
void kb_reset_state(void);                                /* forget held keys (app mode) */
int  kb_ready(void);
const char *kb_layouts(void);
const char *kb_variants(void);
int  kb_group(void);                                      /* active layout */
void kb_set_group(int g);
void kb_reload_compose(void);                             /* after changing the region */
void kb_key(int code, int value, KeyInfo *k);
/* For Wayland: the keymap in XKB format (malloc, NULL if none) and the
 * serialized modifiers. */
char *kb_keymap_string(void);
void kb_mods(uint32_t *depressed, uint32_t *latched, uint32_t *locked, uint32_t *group);

#endif
