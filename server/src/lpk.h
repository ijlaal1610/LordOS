/* lpk.h — paquetes de apps nativas LordOS (.lpk): leer, instalar, listar y quitar (ver lpk.c). */
#ifndef LORD_LPK_H
#define LORD_LPK_H

#include <stddef.h>

#define LPK_MAX_FILES 2000
#define LPK_MAX_TOTAL (64u * 1024 * 1024)     /* contenido descomprimido */

typedef struct {
    char path[224];                     /* relativo, ya validado: "app.js", "wasm/vida.wasm" */
    const unsigned char *data;
    size_t size;
} LpkFile;

typedef struct {
    unsigned char *raw;                 /* el tar en memoria (ya descomprimido) */
    LpkFile *files;
    int nfiles;
} Lpk;

/* Lee un .lpk (tar o tar.gz). Rechaza enlaces, dispositivos, rutas con "..", absolutas o con
 * caracteres raros, y los que pasan los límites. err recibe un texto para el usuario. */
int  lpk_open(const char *path, Lpk *out, char *err, size_t errlen);
const LpkFile *lpk_find(const Lpk *p, const char *path);
void lpk_free(Lpk *p);

/* Carpeta de las apps instaladas: ~/.local/share/lordos/apps (se crea si falta). */
const char *lpk_root(void);
/* Id válido de paquete: [a-z][a-z0-9-]{1,39} (es también su espacio de textos). */
int  lpk_valid_id(const char *id);
/* Ruta relativa válida dentro de un paquete. */
int  lpk_valid_path(const char *rel);
/* Instala (o reemplaza) el paquete en lpk_root()/<id>, de forma atómica. */
int  lpk_install(const Lpk *p, const char *id, char *err, size_t errlen);
int  lpk_remove(const char *id);

#endif
