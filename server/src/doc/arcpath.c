/* arcpath.c — ver arcpath.h. */
#include "arcpath.h"

#include <string.h>

int arc_clean_path(const char *name, char *out, size_t n) {
    if (!name || !out || n < 2) return -1;
    size_t len = strlen(name);
    if (!len || len > ARC_PATH_MAX || name[0] == '/') return -1;
    size_t o = 0;
    int depth = 0;
    const char *p = name;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t k = (size_t)(p - s);
        if (k == 1 && s[0] == '.') continue;
        if (k == 2 && s[0] == '.' && s[1] == '.') return -1;
        if (k > 255) return -1;
        for (size_t i = 0; i < k; i++)
            if ((unsigned char)s[i] < 0x20 || s[i] == 0x7f) return -1;
        if (++depth > ARC_DEPTH_MAX) return -1;
        if (o + (o ? 1 : 0) + k + 1 > n) return -1;
        if (o) out[o++] = '/';
        memcpy(out + o, s, k);
        o += k;
    }
    if (!o) return -1;
    out[o] = 0;
    return 0;
}

int arc_link_ok(const char *entry, const char *target) {
    if (!entry || !target || !target[0] || target[0] == '/' || strlen(target) > ARC_PATH_MAX) return 0;
    /* profundidad de la carpeta que contiene la entrada */
    int depth = 0;
    for (const char *q = entry; *q; q++) if (*q == '/') depth++;
    /* ".." solo al comienzo: subir desde la carpeta real de la entrada y después bajar por
     * nombres. Así, aunque un tramo sea otro enlace del mismo comprimido (que a su vez quedó
     * adentro), la resolución real tampoco sale de la raíz. "a/../.." no se acepta. */
    const char *p = target;
    int down = 0;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t k = (size_t)(p - s);
        for (size_t i = 0; i < k; i++) if ((unsigned char)s[i] < 0x20) return 0;
        if (k == 1 && s[0] == '.') continue;
        if (k == 2 && s[0] == '.' && s[1] == '.') { if (down || --depth < 0) return 0; continue; }
        down = 1;
        depth++;
        if (depth > ARC_DEPTH_MAX * 2) return 0;
    }
    return 1;
}

int arc_under(const char *path, const char *prefix) {
    size_t k = strlen(prefix);
    if (!k) return 1;
    return !strncmp(path, prefix, k) && (path[k] == 0 || path[k] == '/');
}

unsigned arc_file_mode(unsigned mode) { return (mode & 0755u) | 0600u; }
unsigned arc_dir_mode(unsigned mode) { return (mode & 0755u) | 0700u; }
