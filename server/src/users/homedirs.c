/* homedirs.c — carpetas comunes del usuario y xdg-user-dirs (ver homedirs.h). */
#include "homedirs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* La misma lista que tools/iso/rootfs/usr/lib/lordos/lordos-live-prep (allí con _ por espacio). */
static const char *const names[3][HOMEDIRS_COUNT] = {
    { "Escritorio", "Documentos", "Descargas", "Música", "Imágenes", "Videos" },
    { "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos" },
    { "Área de trabalho", "Documentos", "Downloads", "Música", "Imagens", "Vídeos" },
};
static const char *const xdg_keys[HOMEDIRS_COUNT] = { "DESKTOP", "DOCUMENTS", "DOWNLOAD", "MUSIC", "PICTURES", "VIDEOS" };

const char *homedirs_ui_lang(const char *locale) {
    if (locale && !strncmp(locale, "en", 2)) return "en";
    if (locale && !strncmp(locale, "pt", 2)) return "pt";
    return "es";
}

static int lang_index(const char *ui) { return !strcmp(ui, "en") ? 1 : !strcmp(ui, "pt") ? 2 : 0; }

const char *homedirs_name(const char *ui_lang, int i) {
    if (i < 0 || i >= HOMEDIRS_COUNT) return NULL;
    return names[lang_index(ui_lang ? ui_lang : "es")][i];
}

/* Archivo nuevo (O_EXCL, sin seguir enlaces) relativo a la carpeta abierta dfd. */
static int write_new(int dfd, const char *name, const char *text, uid_t uid, gid_t gid) {
    int fd = openat(dfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return errno == EEXIST ? 0 : -1;
    size_t n = strlen(text);
    int ok = write(fd, text, n) == (ssize_t)n;
    if (fchown(fd, uid, gid)) ok = 0;
    if (close(fd)) ok = 0;
    return ok ? 0 : -1;
}

/* Subcarpeta nueva de dfd con dueño; abierta (sin seguir enlaces) o -1. */
static int mkdir_owned(int dfd, const char *name, uid_t uid, gid_t gid) {
    if (mkdirat(dfd, name, 0755) == 0) {
        if (fchownat(dfd, name, uid, gid, AT_SYMLINK_NOFOLLOW)) return -1;
    } else if (errno != EEXIST) return -1;
    return openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

int homedirs_create(const char *home, uid_t uid, gid_t gid, const char *locale) {
    int bad = 0;
    int hfd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (hfd < 0) return -1;
    const char *ui = homedirs_ui_lang(locale);
    int li = lang_index(ui);
    for (int i = 0; i < HOMEDIRS_COUNT; i++) {
        int d = mkdir_owned(hfd, names[li][i], uid, gid);
        if (d < 0) bad = -1; else close(d);
    }
    int cfd = mkdir_owned(hfd, ".config", uid, gid);
    if (cfd >= 0) {
        char text[1024];
        size_t o = (size_t)snprintf(text, sizeof text,
            "# Carpetas del usuario (xdg-user-dirs). Lo escribe LordOS al crear la cuenta.\n");
        for (int i = 0; i < HOMEDIRS_COUNT && o < sizeof text; i++)
            o += (size_t)snprintf(text + o, sizeof text - o, "XDG_%s_DIR=\"$HOME/%s\"\n", xdg_keys[i], names[li][i]);
        if (o < sizeof text) snprintf(text + o, sizeof text - o, "XDG_TEMPLATES_DIR=\"$HOME/\"\nXDG_PUBLICSHARE_DIR=\"$HOME/\"\n");
        if (write_new(cfd, "user-dirs.dirs", text, uid, gid)) bad = -1;
        /* el idioma con el que se nombraron (xdg-user-dirs-update lo compara al cambiar de idioma) */
        char loc[64];
        snprintf(loc, sizeof loc, "%s", li == 1 ? "en_US" : li == 2 ? "pt_BR" : "es_ES");
        if (locale && !strncmp(locale, ui, 2)) { snprintf(loc, sizeof loc, "%s", locale); loc[strcspn(loc, ".@")] = 0; }
        snprintf(text, sizeof text, "%s\n", loc);
        if (write_new(cfd, "user-dirs.locale", text, uid, gid)) bad = -1;
        close(cfd);
    } else bad = -1;
    close(hfd);
    return bad;
}
