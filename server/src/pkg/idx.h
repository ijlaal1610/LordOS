/* idx.h — índice compacto de paquetes de LordOS (/var/lib/lordos/pkg/index.bin).
 *
 * Lo arma el ayudante root en pkg-update a partir de los Packages, Translation-en y DEP-11 ya
 * verificados; lord-server (sin root) lo abre con mmap: nada se copia a memoria hasta que se
 * usa. Todo son enteros de 32 bits little-endian y desplazamientos dentro del bloque de textos
 * (el texto 0 es ""). Los paquetes van ordenados por nombre y, dentro de un nombre, de la
 * versión más nueva a la más vieja. Las descripciones largas van en su propio bloque al final,
 * en el mismo archivo (se reemplaza entero con rename: nunca hay medio índice). */
#ifndef LORD_PKG_IDX_H
#define LORD_PKG_IDX_H

#include <stddef.h>
#include <stdint.h>

#define IDX_MAGIC "LPKIDX2"
#define PKG_DIR "/var/lib/lordos/pkg"
#define PKG_CACHE "/var/cache/lordos/pkg"

enum { IF_ESSENTIAL = 1, IF_PROTECTED = 2, IF_MA_SAME = 4, IF_MA_FOREIGN = 8, IF_MA_ALLOWED = 16 };
enum { PRIO_UNKNOWN, PRIO_REQUIRED, PRIO_IMPORTANT, PRIO_STANDARD, PRIO_OPTIONAL, PRIO_EXTRA };

typedef struct {
    uint32_t name, version, arch, section, summary, filename, homepage, source;
    uint32_t depends, predepends, recommends, suggests, provides, conflicts, breaks, replaces;
    uint32_t size, isize;               /* bytes del .deb, KB instalado */
    uint32_t desc, desclen;             /* en el bloque de descripciones */
    uint32_t app;                       /* índice en apps o UINT32_MAX */
    uint16_t src;                       /* índice en srcs */
    uint8_t prio, flags;
    uint8_t sha256[32];
} IdxPkg;                               /* 120 bytes */

typedef struct { uint32_t name, version, pkg; } IdxProv;      /* name lo provee el paquete pkg */

typedef struct {
    uint32_t id, package, name, summary, name_es, summary_es, name_pt, summary_pt, icon, categories, type;
    uint32_t desc;                      /* en el bloque de descripciones, terminada en 0; UINT32_MAX: no hay */
} IdxApp;

typedef struct {
    uint32_t uri, suite, component, codename, origin, label;
    int64_t date, valid_until;
    uint32_t prio, npkgs;               /* prio: 500 normal, 100 backports, 1 experimental */
} IdxSrc;

typedef struct {
    char magic[8];
    uint32_t npkg, nprov, napp, nsrc;
    uint32_t off_pkg, off_prov, off_app, off_src, off_str, len_str;
    int64_t built;
    uint32_t off_desc, len_desc;
    char arch[16];                      /* arquitectura nativa con la que se armó */
} IdxHeader;

/* ---- lectura (lord-server y el ayudante) ---- */

typedef struct {
    const IdxHeader *h;
    const IdxPkg *pkg;
    const IdxProv *prov;
    const IdxApp *app;
    const IdxSrc *src;
    const char *str, *desc;
    size_t str_len, desc_len, map_len;
    void *map;
    long mtime, ino;
} Idx;

int  idx_open(Idx *x, const char *dir);          /* 0 bien */
void idx_close(Idx *x);
int  idx_refresh(Idx *x, const char *dir);       /* vuelve a abrir si cambió en disco; 1 si cambió */
static inline const char *idx_s(const Idx *x, uint32_t off) { return off < x->str_len ? x->str + off : ""; }
/* Primer paquete con ese nombre (o -1); los siguientes con el mismo nombre están a continuación. */
long idx_find(const Idx *x, const char *name, size_t n);
/* Rango [*lo, *hi) de quienes proveen name. */
void idx_providers(const Idx *x, const char *name, size_t n, long *lo, long *hi);

/* ---- construcción (ayudante root) ---- */

typedef struct IdxBuilder IdxBuilder;
IdxBuilder *idxb_new(void);
int  idxb_source(IdxBuilder *b, const char *uri, const char *suite, const char *component, const char *codename,
                 const char *origin, const char *label, int64_t date, int64_t valid_until, int prio);
/* Packages ya descomprimido de la fuente src (el índice devuelto por idxb_source). */
int  idxb_packages(IdxBuilder *b, int src, const char *buf, size_t len, const char *arch);
/* Translation-en: descripciones largas por Description-md5. */
int  idxb_translation(IdxBuilder *b, const char *buf, size_t len);
/* DEP-11 Components (YAML ya descomprimido). */
int  idxb_appstream(IdxBuilder *b, const char *buf, size_t len);
/* ¿Alguna app usa este ícono (nombre de archivo de icons-64x64.tar)? */
int  idxb_icon_wanted(IdxBuilder *b, const char *name);
int  idxb_write(IdxBuilder *b, const char *dir, const char *arch, char *err, size_t errlen);
void idxb_free(IdxBuilder *b);
uint32_t idxb_count(const IdxBuilder *b);

#endif
