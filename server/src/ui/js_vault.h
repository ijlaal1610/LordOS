/* js_vault.h — lord.vault: Contraseñas y el llavero por el ayudante lord-vault (ver js_vault.c). */
#ifndef LORD_JS_VAULT_H
#define LORD_JS_VAULT_H

#include "quickjs.h"

void js_vault_register(JSContext *c, JSValue lord);

#endif
