/* arcpath_test.c — reglas de extracción segura (src/doc/arcpath.c): nombres de entradas y
 * destinos de enlaces simbólicos que no pueden salir de la carpeta de destino. */
#include "doc/arcpath.h"

#include <stdio.h>
#include <string.h>

static int fails, total;

static void path(const char *in, const char *want) {
    char out[ARC_PATH_MAX + 1];
    int r = arc_clean_path(in, out, sizeof out);
    total++;
    int ok = want ? (r == 0 && !strcmp(out, want)) : r != 0;
    if (!ok) { fails++; printf("FALLA path(\"%s\"): r=%d out=\"%s\" (esperado %s)\n", in, r, r ? "" : out, want ? want : "rechazo"); }
}

static void link_(const char *entry, const char *target, int want) {
    total++;
    int r = arc_link_ok(entry, target);
    if (r != want) { fails++; printf("FALLA link(\"%s\" -> \"%s\") = %d (esperado %d)\n", entry, target, r, want); }
}

static void under(const char *p, const char *pre, int want) {
    total++;
    if (arc_under(p, pre) != want) { fails++; printf("FALLA under(\"%s\", \"%s\")\n", p, pre); }
}

int main(void) {
    /* nombres normales */
    path("a.txt", "a.txt");
    path("dir/sub/a.txt", "dir/sub/a.txt");
    path("./dir//sub/./a.txt", "dir/sub/a.txt");
    path("dir/", "dir");
    path("ñandú/日本語/файл.txt", "ñandú/日本語/файл.txt");
    path("..foo/bar..", "..foo/bar..");                 /* ".." solo como tramo completo */
    path("a\\..\\b", "a\\..\\b");                       /* en Linux la barra invertida es un carácter */
    /* lo que se rechaza */
    path("", NULL);
    path(".", NULL);
    path("./", NULL);
    path("/etc/passwd", NULL);                          /* absoluta */
    path("//etc/passwd", NULL);
    path("../x", NULL);                                 /* sube */
    path("a/../../x", NULL);
    path("a/..", NULL);
    path("a/b/../c", NULL);                             /* aunque quede adentro: no se acepta ".." */
    path("a\nb", NULL);                                 /* control */
    path("a\x7f", NULL);
    char deep[400] = "";
    for (int i = 0; i < 70; i++) strcat(deep, "d/");
    path(deep, NULL);                                   /* más de 64 niveles */
    char longc[300];
    memset(longc, 'x', 256);
    longc[256] = 0;
    path(longc, NULL);                                  /* tramo de más de 255 */
    /* enlaces simbólicos */
    link_("a/link", "b", 1);
    link_("a/link", "../b", 1);                         /* a/../b = b, adentro */
    link_("a/b/link", "../../c", 1);
    link_("a/b/link", "../../../c", 0);                 /* sale */
    link_("link", "..", 0);
    link_("link", "../x", 0);
    link_("link", "/etc/passwd", 0);                    /* absoluto */
    link_("link", "", 0);
    link_("a/link", "./b/./c", 1);
    link_("a/link", "b/../../x", 0);                    /* ".." después de bajar: no */
    link_("a/b/link", "../c/../../d", 0);
    link_("a/link", "x\n", 0);
    /* parte de un comprimido */
    under("a/b/c", "a/b", 1);
    under("a/b", "a/b", 1);
    under("a/bc", "a/b", 0);
    under("x", "", 1);
    /* permisos */
    total++;
    if (arc_file_mode(04777) != 0755 || arc_file_mode(0) != 0600 || arc_dir_mode(02000) != 0700 || arc_file_mode(0666) != 0644) { fails++; printf("FALLA permisos\n"); }
    printf("arcpath: %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}
