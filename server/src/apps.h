/* apps.h — apps Linux instaladas (archivos .desktop) y su lanzador (ver apps.c). */
#ifndef LORD_APPS_H
#define LORD_APPS_H

#include <limits.h>

typedef struct {
    char id[160];                       /* "org.featherpad.FeatherPad" (nombre del .desktop sin la extensión) */
    char name[160], generic[160], comment[320], keywords[320], categories[256];
    char wmclass[160];                  /* StartupWMClass: para reconocer su ventana Wayland */
    char mime[1024];                    /* MimeType: tipos de archivo que abre ("text/plain;…") */
    char icon[PATH_MAX];                /* PNG encontrado, o "" */
    char file[PATH_MAX];                /* el .desktop */
    char bin[128];                      /* nombre del programa de Exec (para el Monitor: proceso → app) */
    int user;                           /* de ~/.local/share/applications */
} DesktopApp;

/* Lista las apps visibles para LordOS (Type=Application, sin NoDisplay/Hidden, OnlyShowIn y
 * NotShowIn, TryExec presente, no de terminal). lang = "es-AR": nombres traducidos.
 * Devuelve la cantidad; *out es un arreglo interno válido hasta la próxima llamada. */
int apps_list(const char *lang, const DesktopApp **out);

/* Lanza la app con ese id: solo una de la lista de arriba, con su línea Exec, sin argumentos
 * del llamador y sin shell. 0 si arrancó el proceso. */
int apps_launch(const char *id);
/* Igual, pero abriendo un archivo o carpeta (ver apps.c: solo apps con MimeType y rutas de la
 * carpeta personal o de los montajes del usuario, como un único argumento). */
int apps_launch_with(const char *id, const char *path);

#endif
