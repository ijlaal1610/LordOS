/* defaults.c — default applications on disk (see defaults.h). */
#include "defaults.h"
#include "mimeapps.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIST_MAX (256 * 1024)

static char *slurp(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > LIST_MAX) { close(fd); return NULL; }
    char *b = malloc((size_t)st.st_size + 1);
    ssize_t r = b ? read(fd, b, (size_t)st.st_size) : -1;
    close(fd);
    if (r < 0) { free(b); return NULL; }
    b[r] = 0;
    return b;
}

static const char *home(void) { const char *h = getenv("HOME"); return h && h[0] == '/' ? h : NULL; }

static int write_atomic(const char *path, const char *text) {
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    int ok = fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    if (fd >= 0) { ok = fsync(fd) == 0 && ok; close(fd); }
    if (!ok || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

int defaults_lookup(const char *mime, char *id, size_t n) {
    if (!mime_valid(mime)) return -1;
    char paths[6][PATH_MAX];
    int np = 0;
    const char *h = home(), *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') snprintf(paths[np++], PATH_MAX, "%s/mimeapps.list", xdg);
    else if (h) snprintf(paths[np++], PATH_MAX, "%s/.config/mimeapps.list", h);
    snprintf(paths[np++], PATH_MAX, "/etc/xdg/mimeapps.list");
    if (h) snprintf(paths[np++], PATH_MAX, "%s/.local/share/applications/mimeapps.list", h);
    snprintf(paths[np++], PATH_MAX, "/usr/local/share/applications/mimeapps.list");
    snprintf(paths[np++], PATH_MAX, "/usr/share/applications/mimeapps.list");
    snprintf(paths[np++], PATH_MAX, "/usr/share/applications/defaults.list");
    char *removed_in = NULL;                    /* the user's file: its Removed Associations apply below */
    for (int i = 0; i < np; i++) {
        char *t = slurp(paths[i]);
        if (!t) continue;
        int found = mimeapps_default(t, mime, id, n) == 0 && !(removed_in && mimeapps_removed(removed_in, mime, id));
        if (i == 0) removed_in = t;
        else free(t);
        if (found) { free(removed_in); return 0; }
    }
    free(removed_in);
    return -1;
}

int defaults_set(const char *mime, const char *id) {
    const char *h = home();
    if (!h || !mime_valid(mime) || !desktop_id_valid(id)) return -1;
    char dir[PATH_MAX], path[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/.config", h);
    mkdir(dir, 0700);
    snprintf(path, sizeof path, "%s/.config/mimeapps.list", h);
    char *old = slurp(path);
    char *now = mimeapps_set_default(old, mime, id);
    free(old);
    if (!now) return -1;
    int r = write_atomic(path, now);
    free(now);
    return r;
}

int defaults_guess(const char *path, char *mime, size_t n) {
    static char *globs;
    static int tried;
    if (!tried) { tried = 1; globs = slurp("/usr/share/mime/globs2"); }
    return globs ? mime_guess(globs, path, mime, n) : -1;
}

int defaults_app_for(const char *path, char *id, size_t n) {
    char mime[MIME_MAX], d[DESKTOP_ID_MAX];
    if (defaults_guess(path, mime, sizeof mime) || defaults_lookup(mime, d, sizeof d)) return -1;
    if (!strncmp(d, "lordos-", 7)) return -1;            /* one of ours: the theme opens it */
    size_t l = strlen(d) - 8;                             /* without ".desktop" */
    if (l >= n) return -1;
    memcpy(id, d, l);
    id[l] = 0;
    return 0;
}

int defaults_terminal(char *id, size_t n) {
    const char *h = home();
    if (!h) return -1;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/.config/xdg-terminals.list", h);
    char *t = slurp(path);
    if (!t) return -1;
    int r = -1;
    for (char *line = strtok(t, "\n"); line && r; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '+') line++;     /* "+" marks a preferred entry in the spec */
        if (*line == '#' || !*line) continue;
        char *sp = strpbrk(line, " :\t");
        if (sp) *sp = 0;
        if (desktop_id_valid(line) && strlen(line) < n) { snprintf(id, n, "%s", line); r = 0; }
    }
    free(t);
    return r;
}

int defaults_set_terminal(const char *id) {
    const char *h = home();
    if (!h || !desktop_id_valid(id)) return -1;
    char path[PATH_MAX], text[DESKTOP_ID_MAX + 96];
    snprintf(path, sizeof path, "%s/.config", h);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/.config/xdg-terminals.list", h);
    snprintf(text, sizeof text, "# Written by LordOS (Settings → Default Apps)\n%s\n", id);
    return write_atomic(path, text);
}
