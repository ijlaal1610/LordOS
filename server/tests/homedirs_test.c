/* homedirs_test.c — carpetas comunes del usuario (src/users/homedirs.c): las de cada idioma,
 * user-dirs.dirs y user-dirs.locale, que una segunda vez no pisa nada y que no sigue enlaces.
 * Corre sin root: el dueño es el propio usuario. */
#include "users/homedirs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FALLA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int is_dir(const char *base, const char *name) {
    char p[512];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", base, name);
    return !lstat(p, &st) && S_ISDIR(st.st_mode);
}

static char *slurp(const char *base, const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", base, name);
    FILE *f = fopen(p, "r");
    if (!f) return NULL;
    static char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void lang(const char *locale, const char *const want[6], const char *xdg_doc, const char *loc) {
    char home[] = "/tmp/homedirs-test-XXXXXX";
    if (!mkdtemp(home)) { perror("mkdtemp"); exit(1); }
    CHECK(homedirs_create(home, getuid(), getgid(), locale) == 0, "%s: homedirs_create", locale);
    for (int i = 0; i < 6; i++) {
        CHECK(is_dir(home, want[i]), "%s: falta %s", locale, want[i]);
        CHECK(!strcmp(homedirs_name(homedirs_ui_lang(locale), i), want[i]), "%s: nombre %d", locale, i);
    }
    char *d = slurp(home, ".config/user-dirs.dirs");
    CHECK(d && strstr(d, xdg_doc), "%s: user-dirs.dirs sin %s", locale, xdg_doc);
    CHECK(d && strstr(d, "XDG_TEMPLATES_DIR=\"$HOME/\""), "%s: plantillas", locale);
    char *l = slurp(home, ".config/user-dirs.locale");
    CHECK(l && !strncmp(l, loc, strlen(loc)), "%s: user-dirs.locale = %s", locale, l ? l : "(nada)");
    /* una segunda vez: no falla y no pisa lo que el usuario cambió */
    char p[512];
    snprintf(p, sizeof p, "%s/.config/user-dirs.dirs", home);
    FILE *f = fopen(p, "w");
    if (f) { fputs("XDG_DOCUMENTS_DIR=\"$HOME/Mis cosas\"\n", f); fclose(f); }
    CHECK(homedirs_create(home, getuid(), getgid(), locale) == 0, "%s: segunda vez", locale);
    d = slurp(home, ".config/user-dirs.dirs");
    CHECK(d && strstr(d, "Mis cosas"), "%s: pisó user-dirs.dirs", locale);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd)) { /* nada */ }
}

int main(void) {
    static const char *const es[6] = { "Escritorio", "Documentos", "Descargas", "Música", "Imágenes", "Videos" };
    static const char *const en[6] = { "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos" };
    static const char *const pt[6] = { "Área de trabalho", "Documentos", "Downloads", "Música", "Imagens", "Vídeos" };
    lang("es_AR.UTF-8", es, "XDG_DOCUMENTS_DIR=\"$HOME/Documentos\"", "es_AR");
    lang("en_US.UTF-8", en, "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"", "en_US");
    lang("pt_BR.UTF-8", pt, "XDG_DESKTOP_DIR=\"$HOME/Área de trabalho\"", "pt_BR");
    lang("C.UTF-8", es, "XDG_PICTURES_DIR=\"$HOME/Imágenes\"", "es_ES");     /* sin idioma: español */

    /* ~/.config como enlace a otro lado: no se escribe a través de él */
    char home[] = "/tmp/homedirs-test-XXXXXX", other[] = "/tmp/homedirs-other-XXXXXX";
    if (!mkdtemp(home) || !mkdtemp(other)) return 1;
    char link[512];
    snprintf(link, sizeof link, "%s/.config", home);
    if (symlink(other, link)) return 1;
    CHECK(homedirs_create(home, getuid(), getgid(), "es_ES.UTF-8") != 0, "enlace: tendría que fallar");
    CHECK(!slurp(other, "user-dirs.dirs"), "enlace: escribió a través de ~/.config");
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", home, other);
    if (system(cmd)) { /* nada */ }

    printf("homedirs: %s\n", fails ? "FALLA" : "ok");
    return fails ? 1 : 0;
}
