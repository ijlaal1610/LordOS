/* js_sys.h — APIs de sistema expuestas al JS (archivos, sistema, ajustes, temas). */
#ifndef LORD_JS_SYS_H
#define LORD_JS_SYS_H

#include "quickjs.h"

void js_sys_init(void);                         /* toma $HOME (ya sin privilegios) */
void js_sys_register(JSContext *c, JSValue lord);
/* Proceso de una app aislada: solo lord.text y lord.image (dibujo y dirección del texto). */
void js_sys_register_app(JSContext *c, JSValue lord);
void js_sys_set_greeter(int on);             /* tema de la pantalla de ingreso: lord.greeter */
void js_sys_set_assets(const char *dir);   /* carpeta assets/ (catálogos de idioma) */
const char *js_sys_assets(void);          /* para src: 'asset:icons/x.png' */
void js_sys_cpu_name(char *out, size_t size);   /* procesador (x86 o ARM; lo usa también el Monitor) */
/* Ruta de destino válida para escribir (carpeta personal o montajes del usuario, nunca las
 * carpetas protegidas): 0 si sirve; out recibe la ruta real. */
int js_sys_resolve_target(const char *path, char *out, size_t n);

#endif
