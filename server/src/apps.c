/* apps.c — installed Linux apps: reads the .desktop files from /usr/share/applications and
 * ~/.local/share/applications (Desktop Entry Specification 1.5), finds their PNG icon and launches them.
 *
 * It is the second exception to "the server does not run commands" (approved for M7; the first
 * is the Terminal's shell). Limits:
 *   - an app is only launched by its id, and the id must belong to a .desktop file in those two
 *     folders that passes the apps_list() filters;
 *   - the command is that file's Exec line; the caller adds no arguments or
 *     files (the %f %u… codes are removed) and it does not go through a shell: argv is built per the
 *     specification and run with execv;
 *   - terminal apps (Terminal=true) are neither listed nor launched;
 *   - JS cannot create .desktop files: lord.fs is read-only and LordOS app packages are installed
 *     in another folder (~/.local/share/lordos/apps), so a command cannot be
 *     invented from the UI.
 * App output goes to ~/.cache/lordos/apps.log (trimmed when it exceeds 1 MB).
 *
 * An app the owner restricted with "no access to your files" (Settings → Privacy, firewall id
 * desktop:<id>, sec/restrict.h) starts under Landlock (sandbox_apply_files): only its own config,
 * cache and data folders, the system read-only, /tmp and the runtime dir, plus the one file or
 * folder it was opened with. A restriction set while the app runs applies at its next launch. */
#include "apps.h"
#include "hw/defaults.h"
#include "fsops.h"
#include "sec/cgroup.h"
#include "sec/restrict.h"
#include "sec/sandbox.h"
#include "fw/rules.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MAX_APPS 1024

static DesktopApp *apps;
static int napps;
static char cached_lang[32];
static time_t cached_mtime[2];

/* ---- reading a .desktop file ---- */

/* Removes the escapes of a string-type value (\s \n \t \r \\). */
static void unescape(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            *o++ = *s == 's' ? ' ' : *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'r' ? '\r' : *s;
        } else *o++ = *s;
    }
    *o = 0;
}

/* Priority of a translated key for the language: Name[es_AR] > Name[es] > Name. */
static int locale_rank(const char *loc, const char *lang) {
    if (!loc) return 1;
    char ll[8] = "", cc[8] = "";
    sscanf(lang, "%7[a-z]%*[-_]%7[A-Za-z]", ll, cc);
    char want[20];
    snprintf(want, sizeof want, "%s_%s", ll, cc);
    if (cc[0] && !strcmp(loc, want)) return 3;
    if (!strcmp(loc, ll)) return 2;
    return 0;
}

typedef struct {
    DesktopApp a;
    char exec[1024], tryexec[256], icon_name[256], path[512];
    char only[256], not_[256];
    int ranks[5];                       /* name generic comment keywords: language priority */
    int hidden, nodisplay, terminal, is_app, has_name;
} Entry;

static void set_tr(Entry *e, int slot, char *dst, size_t n, const char *val, const char *loc, const char *lang) {
    int r = locale_rank(loc, lang);
    if (!r && loc) return;
    if (r < e->ranks[slot]) return;
    e->ranks[slot] = r;
    snprintf(dst, n, "%s", val);
}

static int parse_desktop(const char *file, const char *lang, Entry *e) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    memset(e, 0, sizeof *e);
    char line[2048];
    int in_main = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { in_main = !strcmp(line, "[Desktop Entry]"); continue; }
        if (!in_main || line[0] == '#' || !line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *k = line, *v = eq + 1;
        char *ke = eq;
        while (ke > k && ke[-1] == ' ') ke--;
        *ke = 0;
        while (*v == ' ') v++;
        char *loc = NULL, *br = strchr(k, '[');
        if (br) { *br = 0; loc = br + 1; char *cb = strchr(loc, ']'); if (cb) *cb = 0; char *at = strchr(loc, '@'); if (at) *at = 0; char *dot = strchr(loc, '.'); if (dot) *dot = 0; }
        if (!strcmp(k, "Name")) { set_tr(e, 0, e->a.name, sizeof e->a.name, v, loc, lang); e->has_name = 1; }
        else if (!strcmp(k, "GenericName")) set_tr(e, 1, e->a.generic, sizeof e->a.generic, v, loc, lang);
        else if (!strcmp(k, "Comment")) set_tr(e, 2, e->a.comment, sizeof e->a.comment, v, loc, lang);
        else if (!strcmp(k, "Keywords")) set_tr(e, 3, e->a.keywords, sizeof e->a.keywords, v, loc, lang);
        else if (loc) continue;
        else if (!strcmp(k, "Type")) e->is_app = !strcmp(v, "Application");
        else if (!strcmp(k, "Exec")) snprintf(e->exec, sizeof e->exec, "%s", v);
        else if (!strcmp(k, "TryExec")) snprintf(e->tryexec, sizeof e->tryexec, "%s", v);
        else if (!strcmp(k, "Icon")) snprintf(e->icon_name, sizeof e->icon_name, "%s", v);
        else if (!strcmp(k, "Path")) snprintf(e->path, sizeof e->path, "%s", v);
        else if (!strcmp(k, "Categories")) snprintf(e->a.categories, sizeof e->a.categories, "%s", v);
        else if (!strcmp(k, "MimeType")) snprintf(e->a.mime, sizeof e->a.mime, "%s", v);
        else if (!strcmp(k, "StartupWMClass")) snprintf(e->a.wmclass, sizeof e->a.wmclass, "%s", v);
        else if (!strcmp(k, "OnlyShowIn")) snprintf(e->only, sizeof e->only, "%s", v);
        else if (!strcmp(k, "NotShowIn")) snprintf(e->not_, sizeof e->not_, "%s", v);
        else if (!strcmp(k, "Hidden")) e->hidden = !strcmp(v, "true");
        else if (!strcmp(k, "NoDisplay")) e->nodisplay = !strcmp(v, "true");
        else if (!strcmp(k, "Terminal")) e->terminal = !strcmp(v, "true");
    }
    fclose(f);
    unescape(e->a.name); unescape(e->a.generic); unescape(e->a.comment);
    snprintf(e->a.file, sizeof e->a.file, "%s", file);
    return 0;
}

/* Program name of an Exec line ("env VAR=1 /usr/bin/foo %u" → "foo"), only to
 * recognize its processes in the Monitor; never used to launch anything. */
static void exec_bin(const char *exec, char *out, size_t n) {
    out[0] = 0;
    const char *p = exec;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) return;
        const char *q = p;
        char quote = *p == '"' ? '"' : 0;
        if (quote) q = ++p;
        while (*q && (quote ? *q != '"' : *q != ' ')) q++;
        size_t len = (size_t)(q - p);
        int is_env = len == 3 && !strncmp(p, "env", 3), is_var = memchr(p, '=', len) != NULL;
        if (!is_env && !is_var) {
            const char *base = p;
            for (const char *c = p; c < q; c++) if (*c == '/') base = c + 1;
            snprintf(out, n, "%.*s", (int)(q - base), base);
            return;
        }
        p = *q ? q + 1 : q;
    }
}

static int list_has(const char *list, const char *item) {
    size_t n = strlen(item);
    for (const char *p = list; *p;) {
        const char *q = strchr(p, ';');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (len == n && !strncmp(p, item, n)) return 1;
        if (!q) break;
        p = q + 1;
    }
    return 0;
}

/* Program of the Exec line in the fixed PATH (plus ~/.local/bin for the user's .desktop files). */
static int find_program(const char *prog, int user, char *out, size_t n) {
    if (!prog[0]) return -1;
    if (strchr(prog, '/')) {
        if (prog[0] != '/' || access(prog, X_OK)) return -1;
        snprintf(out, n, "%s", prog);
        return 0;
    }
    const char *home = getenv("HOME");
    const char *dirs[] = { "/usr/local/bin", "/usr/bin", "/bin", "/usr/local/games", "/usr/games", NULL, NULL };
    char local[PATH_MAX];
    if (user && home) { snprintf(local, sizeof local, "%s/.local/bin", home); dirs[5] = local; }
    for (int i = 0; dirs[i]; i++) {
        snprintf(out, n, "%s/%s", dirs[i], prog);
        struct stat st;
        if (!stat(out, &st) && S_ISREG(st.st_mode) && !access(out, X_OK)) return 0;
    }
    return -1;
}

/* ---- icons: hicolor PNGs (and from the user's theme), or /usr/share/pixmaps ---- */

static int is_png(const char *path) {
    size_t n = strlen(path);
    struct stat st;
    return n > 4 && !strcmp(path + n - 4, ".png") && !stat(path, &st) && S_ISREG(st.st_mode);
}

static void find_icon(const char *name, char *out, size_t n) {
    out[0] = 0;
    if (!name[0] || strstr(name, "..")) return;
    if (name[0] == '/') { if (is_png(name)) snprintf(out, n, "%s", name); return; }
    static const int sizes[] = { 64, 48, 128, 96, 256, 72, 32, 512 };
    const char *home = getenv("HOME");
    char roots[3][PATH_MAX];
    int nr = 0;
    if (home) snprintf(roots[nr++], PATH_MAX, "%s/.local/share/icons/hicolor", home);
    snprintf(roots[nr++], PATH_MAX, "/usr/share/icons/hicolor");
    for (int r = 0; r < nr; r++)
        for (size_t s = 0; s < sizeof sizes / sizeof *sizes; s++) {
            int w = snprintf(out, n, "%s/%dx%d/apps/%s.png", roots[r], sizes[s], sizes[s], name);
            if (w > 0 && (size_t)w < n && is_png(out)) return;
        }
    int w = snprintf(out, n, "/usr/share/pixmaps/%s.png", name);
    if (w > 0 && (size_t)w < n && is_png(out)) return;
    /* last resort: the 64 px AppStream PNG downloaded by the package engine
     * (/var/lib/lordos/pkg/icons/64/<package>_<icon>.png): useful for apps that only ship SVG */
    out[0] = 0;
    DIR *d = opendir("/var/lib/lordos/pkg/icons/64");
    if (!d) return;
    char want[300];
    snprintf(want, sizeof want, "_%s.png", name);
    size_t wl = strlen(want);
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > wl && !strcmp(e->d_name + l - wl, want)) {
            w = snprintf(out, n, "/var/lib/lordos/pkg/icons/64/%s", e->d_name);
            if (w > 0 && (size_t)w < n && is_png(out)) break;
            out[0] = 0;
        }
    }
    closedir(d);
}

/* ---- list ---- */

static int valid_id(const char *id) {
    size_t n = strlen(id);
    if (!n || n >= sizeof apps[0].id) return 0;
    for (const char *p = id; *p; p++) if (!isalnum((unsigned char)*p) && !strchr("._-+", *p)) return 0;
    return id[0] != '.' && id[0] != '-';
}

static void app_dirs(char dirs[2][PATH_MAX]) {
    const char *home = getenv("HOME");
    snprintf(dirs[0], PATH_MAX, "%s/.local/share/applications", home ? home : "/nonexistent");
    snprintf(dirs[1], PATH_MAX, "/usr/share/applications");
}

static int cmp_name(const void *a, const void *b) {
    return strcasecmp(((const DesktopApp *)a)->name, ((const DesktopApp *)b)->name);
}

/* Is the id already there? (the user's come first and shadow the system's) */
static int seen(const char *id, char (*ids)[160], int n) {
    for (int i = 0; i < n; i++) if (!strcmp(ids[i], id)) return 1;
    return 0;
}

int apps_list(const char *lang, const DesktopApp **out) {
    char dirs[2][PATH_MAX];
    app_dirs(dirs);
    time_t mt[2];
    for (int d = 0; d < 2; d++) { struct stat st; mt[d] = stat(dirs[d], &st) ? 0 : st.st_mtime; }
    if (apps && !strcmp(cached_lang, lang) && mt[0] == cached_mtime[0] && mt[1] == cached_mtime[1]) { *out = apps; return napps; }

    DesktopApp *list = calloc(MAX_APPS, sizeof *list);
    char (*ids)[160] = calloc(MAX_APPS * 2, sizeof *ids);
    Entry *e = malloc(sizeof *e);
    int n = 0, nids = 0;
    for (int d = 0; d < 2 && list && ids && e; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) && n < MAX_APPS) {
            size_t len = strlen(de->d_name);
            if (len < 9 || strcmp(de->d_name + len - 8, ".desktop")) continue;
            char id[160];
            snprintf(id, sizeof id, "%.*s", (int)(len - 8), de->d_name);
            if (!valid_id(id) || seen(id, ids, nids)) continue;
            if (nids < MAX_APPS * 2) snprintf(ids[nids++], sizeof ids[0], "%s", id);   /* it also shadows if hidden */
            char file[PATH_MAX * 2];
            int flen = snprintf(file, sizeof file, "%s/%s", dirs[d], de->d_name);
            if (flen < 0 || flen >= PATH_MAX || parse_desktop(file, lang, e)) continue;
            if (!e->is_app || e->hidden || e->nodisplay || e->terminal || !e->has_name || !e->exec[0]) continue;
            if (e->only[0] && !list_has(e->only, "LordOS")) continue;
            if (e->not_[0] && list_has(e->not_, "LordOS")) continue;
            char prog[PATH_MAX];
            if (e->tryexec[0] && find_program(e->tryexec, d == 0, prog, sizeof prog)) continue;
            DesktopApp *a = &list[n++];
            *a = e->a;
            snprintf(a->id, sizeof a->id, "%s", id);
            a->user = d == 0;
            find_icon(e->icon_name, a->icon, sizeof a->icon);
            exec_bin(e->exec, a->bin, sizeof a->bin);
        }
        closedir(dir);
    }
    free(ids);
    free(e);
    if (!list) return 0;
    qsort(list, (size_t)n, sizeof *list, cmp_name);
    free(apps);
    apps = list;
    napps = n;
    snprintf(cached_lang, sizeof cached_lang, "%s", lang);
    cached_mtime[0] = mt[0];
    cached_mtime[1] = mt[1];
    *out = apps;
    return napps;
}

/* ---- launch ---- */

/* Builds argv from the Exec line (specification, "The Exec key"): arguments separated by
 * spaces, double quotes with \" \` \$ \\, field codes. With file (apps_launch_with), the
 * first %f/%F goes as the path and the first %u/%U as file://…; the other file codes
 * are removed. %i → --icon <icon>; %c → name; %k → the .desktop file; %% → %. *used tells whether file was used. */
#define MAX_ARGS 64
static void file_uri(const char *path, char *out, size_t n) {
    size_t o = (size_t)snprintf(out, n, "file://");
    for (const unsigned char *p = (const unsigned char *)path; *p && o + 4 < n; p++) {
        if (isalnum(*p) || strchr("/-_.~", *p)) out[o++] = (char)*p;
        else o += (size_t)snprintf(out + o, n - o, "%%%02X", *p);
    }
    out[o] = 0;
}

static int split_exec(const char *exec, const Entry *e, char **argv, char *buf, size_t cap, const char *file, int *used) {
    int argc = 0;
    size_t o = 0;
    const char *p = exec;
    while (*p && argc < MAX_ARGS - 3) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        size_t start = o;
        int quoted = 0, field_only = 0, empty_ok = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (o + 4 >= cap) return -1;
            if (*p == '"') { quoted = !quoted; empty_ok = 1; p++; continue; }
            if (quoted && *p == '\\' && p[1] && strchr("\"`$\\", p[1])) { buf[o++] = p[1]; p += 2; continue; }
            if (*p == '%' && p[1]) {
                char c = p[1];
                p += 2;
                if (c == '%') buf[o++] = '%';
                else if (c == 'c') { size_t l = strlen(e->a.name); if (o + l >= cap) return -1; memcpy(buf + o, e->a.name, l); o += l; }
                else if (c == 'k') { size_t l = strlen(e->a.file); if (o + l >= cap) return -1; memcpy(buf + o, e->a.file, l); o += l; }
                else if (c == 'i') field_only = e->icon_name[0] ? 2 : 1;
                else if (file && !*used && strchr("fFuU", c)) {
                    char uri[PATH_MAX * 3];
                    const char *v = file;
                    if (c == 'u' || c == 'U') { file_uri(file, uri, sizeof uri); v = uri; }
                    size_t l = strlen(v);
                    if (o + l + 1 >= cap) return -1;
                    memcpy(buf + o, v, l);
                    o += l;
                    *used = 1;
                    empty_ok = 1;
                }
                else field_only = 1;            /* the other file codes and the deprecated ones */
                continue;
            }
            buf[o++] = *p++;
        }
        if (quoted) return -1;                  /* unclosed quotes */
        if (field_only == 2 && o == start) {
            argv[argc++] = "--icon";
            size_t l = strlen(e->icon_name);
            if (o + l + 1 >= cap) return -1;
            memcpy(buf + o, e->icon_name, l + 1);
            argv[argc++] = buf + start;
            o += l + 1;
            continue;
        }
        if (o == start && !empty_ok) continue;  /* it was only a field code */
        buf[o++] = 0;
        argv[argc++] = buf + start;
    }
    argv[argc] = NULL;
    return argc;
}

static void open_log(void) {
    const char *home = getenv("HOME");
    char path[PATH_MAX];
    if (!home) return;
    snprintf(path, sizeof path, "%s/.cache", home);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/.cache/lordos", home);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/.cache/lordos/apps.log", home);
    struct stat st;
    int flags = O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW;
    if (!lstat(path, &st) && st.st_size > 1024 * 1024) flags |= O_TRUNC;
    int fd = open(path, flags, 0600);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
}

/* ---- "no access to your files": the Landlock plan (built before fork) ---- */

typedef struct {
    SandboxFilesCfg cfg;
    char buf[2 * SANDBOX_FILES_MAX][PATH_MAX];
    int nbuf, nrw, nro;
} FilesPlan;

static const char *plan_str(FilesPlan *p, const char *fmt, const char *a, const char *b) {
    if (p->nbuf >= 2 * SANDBOX_FILES_MAX) return NULL;
    int n = snprintf(p->buf[p->nbuf], PATH_MAX, fmt, a, b);
    if (n < 0 || n >= PATH_MAX) return NULL;
    return p->buf[p->nbuf++];
}

static void plan_add(FilesPlan *p, int rw, const char *fmt, const char *a, const char *b) {
    int *n = rw ? &p->nrw : &p->nro;
    const char **list = rw ? p->cfg.rw : p->cfg.ro;
    if (*n >= SANDBOX_FILES_MAX) return;
    const char *s = plan_str(p, fmt, a, b);
    if (!s) return;
    for (int i = 0; i < *n; i++) if (!strcmp(list[i], s)) { p->nbuf--; return; }
    list[(*n)++] = s;
    list[*n] = NULL;
}

/* A name that may become a folder of its own under ~/.config, ~/.cache or ~/.local/share:
 * plain characters, not hidden, and never one of the folders that hold the user's secrets or
 * LordOS's own decisions. */
static int dir_name_ok(const char *c) {
    static const char *const deny[] = { "lordos", "ssh", "gnupg", "local", "config", "cache", "pki", "password-store",
                                        "thumbnails", "autostart", "systemd", "environment.d", "dconf", "keyrings", NULL };
    size_t n = strlen(c);
    if (!n || n > 100 || c[0] == '.' || c[0] == '-') return 0;
    for (const char *q = c; *q; q++) if (!isalnum((unsigned char)*q) && !strchr("._-+", *q)) return 0;
    for (int i = 0; deny[i]; i++) if (!strcasecmp(c, deny[i])) return 0;
    return 1;
}

/* The app's own names: its .desktop id, the last part of a reverse-DNS id, the program and its
 * window class, as written and in lowercase. */
static int app_names(const DesktopApp *a, char out[][160], int max) {
    char tmp[6][160];
    int nt = 0;
    snprintf(tmp[nt++], 160, "%.159s", a->id);
    const char *dot = strrchr(a->id, '.');
    if (dot && dot[1]) snprintf(tmp[nt++], 160, "%.159s", dot + 1);
    if (a->bin[0]) snprintf(tmp[nt++], 160, "%.159s", a->bin);
    static const char *const suffixes[] = { "-esr", "-stable", "-bin", NULL };
    for (int i = 0; suffixes[i] && a->bin[0]; i++) {
        size_t bl = strlen(a->bin), sl = strlen(suffixes[i]);
        if (bl > sl && !strcmp(a->bin + bl - sl, suffixes[i])) { snprintf(tmp[nt++], 160, "%.*s", (int)(bl - sl), a->bin); break; }
    }
    if (a->wmclass[0]) snprintf(tmp[nt++], 160, "%.159s", a->wmclass);
    int n = 0;
    for (int i = 0; i < nt; i++)
        for (int lower = 0; lower < 2; lower++) {
            char c[160];
            memcpy(c, tmp[i], sizeof c);
            c[sizeof c - 1] = 0;
            if (lower) for (char *q = c; *q; q++) *q = (char)tolower((unsigned char)*q);
            if (!dir_name_ok(c)) continue;
            int dup = 0;
            for (int k = 0; k < n; k++) dup |= !strcmp(out[k], c);
            if (!dup && n < max) memcpy(out[n++], c, sizeof c);
        }
    return n;
}

static int is_dir(const char *p) { struct stat st; return !lstat(p, &st) && S_ISDIR(st.st_mode); }

/* What a restricted app may touch (see sandbox.h). file: the realpath it was opened with, or NULL. */
static FilesPlan *files_plan(const DesktopApp *a, const char *file) {
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') return NULL;
    FilesPlan *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->cfg.runtime_dir = getenv("XDG_RUNTIME_DIR");
    char names[12][160];
    int nn = app_names(a, names, 12), have_config = 0;
    char path[PATH_MAX];
    for (int i = 0; i < nn; i++) {
        static const char *const own[] = { "%s/.config/%s", "%s/.local/share/%s", "%s/.local/state/%s", "%s/.cache/%s", "%s/.%s", NULL };
        for (int k = 0; own[k]; k++) {
            snprintf(path, sizeof path, own[k], home, names[i]);
            if (!is_dir(path)) continue;               /* only what exists (no symlinks out of the home) */
            plan_add(p, 1, "%s%s", path, "");
            if (k == 0 || k == 4) have_config = 1;
        }
    }
    /* programs whose data folder isn't named after them (program name prefix → folder in the
     * home and in ~/.cache); created if missing, like the app would */
    static const struct { const char *prog, *dir; } alias[] = { { "firefox", "mozilla" }, { "librewolf", "librewolf" },
                                                               { "icecat", "mozilla" }, { NULL, NULL } };
    for (int k = 0; alias[k].prog && a->bin[0]; k++) {
        if (strncmp(a->bin, alias[k].prog, strlen(alias[k].prog))) continue;
        snprintf(path, sizeof path, "%s/.%s", home, alias[k].dir);
        mkdir(path, 0700);
        if (is_dir(path)) { plan_add(p, 1, "%s%s", path, ""); have_config = 1; }
        snprintf(path, sizeof path, "%s/.cache/%s", home, alias[k].dir);
        mkdir(path, 0700);
        if (is_dir(path)) plan_add(p, 1, "%s%s", path, "");
    }
    /* none yet: its folders under the program's name, so it can save its settings */
    if (!have_config && a->bin[0] && dir_name_ok(a->bin)) {
        static const char *const make[] = { "%s/.config", "%s/.config/%s", "%s/.local", "%s/.local/share", "%s/.local/share/%s",
                                            "%s/.cache", "%s/.cache/%s", NULL };
        for (int k = 0; make[k]; k++) { snprintf(path, sizeof path, make[k], home, a->bin); mkdir(path, 0700); }
        plan_add(p, 1, "%s/.config/%s", home, a->bin);
        plan_add(p, 1, "%s/.local/share/%s", home, a->bin);
        plan_add(p, 1, "%s/.cache/%s", home, a->bin);
    }
    /* caches shared by every app that hold nothing of the user's (not all of ~/.cache: its
     * thumbnails are pictures of the user's documents) */
    static const char *const caches[] = { "fontconfig", "mesa_shader_cache", "mesa_shader_cache_db", "radv_builtin_shaders",
                                          "gstreamer-1.0", "ibus", "at-spi", NULL };
    for (int k = 0; caches[k]; k++) plan_add(p, 1, "%s/.cache/%s", home, caches[k]);
    /* the desktop's look and settings, read only */
    static const char *const look[] = { ".config/gtk-3.0", ".config/gtk-4.0", ".config/fontconfig", ".config/dconf",
                                        ".config/mimeapps.list", ".config/user-dirs.dirs", ".config/user-dirs.locale",
                                        ".config/kdeglobals", ".config/Trolltech.conf", ".config/qt5ct", ".config/qt6ct",
                                        ".config/pulse", ".local/share/fonts", ".local/share/icons", ".local/share/themes",
                                        ".local/share/mime", ".local/share/applications", ".fonts", ".icons", ".themes", NULL };
    for (int k = 0; look[k]; k++) plan_add(p, 0, "%s/%s", home, look[k]);
    /* "Open with": that file or folder only */
    if (file) plan_add(p, 1, "%s%s", file, "");
    return p;
}

static int launch(const char *id, const char *file);
int apps_launch(const char *id) { return launch(id, NULL); }

/* Opening a file with an app: only an app that declares MimeType (accepts files), a real
 * path inside the home folder or /media/$USER (fsops_allowed), a regular file or folder,
 * and only if its Exec has a file code: the path goes as ONE argument. */
int apps_launch_with(const char *id, const char *path) {
    char real[PATH_MAX];
    struct stat st;
    if (!path || !realpath(path, real) || !fsops_allowed(real) || stat(real, &st) || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) return -1;
    /* id "" = the default app for the file's type (Settings → Default Apps, ~/.config/mimeapps.list);
     * -1 if that is one of LordOS's own apps (the theme opens those) or there is none */
    char def[160];
    if (id && !id[0]) { if (defaults_app_for(real, def, sizeof def)) return -1; id = def; }
    return launch(id, real);
}

static int launch(const char *id, const char *file) {
    if (!valid_id(id)) return -1;
    const DesktopApp *list;
    int n = apps_list(cached_lang[0] ? cached_lang : "en", &list);
    const DesktopApp *a = NULL;
    for (int i = 0; i < n; i++) if (!strcmp(list[i].id, id)) { a = &list[i]; break; }
    if (!a) return -1;
    /* the file is read again: what counts is what it says now */
    Entry *e = malloc(sizeof *e);
    char *buf = malloc(4096);
    char *argv[MAX_ARGS];
    char prog[PATH_MAX];
    int used = 0;
    int ok = e && buf && !parse_desktop(a->file, cached_lang, e) && e->is_app && !e->terminal && !e->hidden &&
             (!file || e->a.mime[0]) &&
             split_exec(e->exec, e, argv, buf, 4096, file, &used) > 0 && (!file || used) && !find_program(argv[0], a->user, prog, sizeof prog);
    char cwd[PATH_MAX] = "";
    if (ok && e->path[0] == '/') snprintf(cwd, sizeof cwd, "%s", e->path);
    free(e);
    if (!ok) { free(buf); return -1; }
    /* "no access to your files": Landlock before execve. Without Landlock in the kernel the app
     * doesn't start (the owner asked for it to be kept away from their files). */
    FilesPlan *plan = NULL;
    char fwid[FW_APP_MAX];
    snprintf(fwid, sizeof fwid, "desktop:%s", id);
    if (restrict_blocks(fwid, FW_R_NO_FILES)) {
        if (!sandbox_landlock_abi() || !(plan = files_plan(a, file))) {
            fprintf(stderr, "apps: %s is restricted to no files, but Landlock is not available: not launched\n", id);
            free(buf);
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        /* orphaned grandchild: it does not remain a child of lord-server */
        setsid();
        if (fork() != 0) _exit(0);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (int s = 1; s < 32; s++) signal(s, SIG_DFL);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
        open_log();
        syscall(SYS_close_range, 3, ~0u, 0);
        const char *home = getenv("HOME");
        if (!cwd[0] || chdir(cwd)) { if (home && chdir(home)) {} }
        app_scope_enter(id);        /* its own app-lordos-<id>.scope cgroup (the monitor attributes it) */
        if (plan && sandbox_apply_files(&plan->cfg)) {
            static const char msg[] = "apps: could not apply the no-files restriction (Landlock): not launched\n";
            if (write(2, msg, sizeof msg - 1) < 0) {}
            _exit(126);
        }
        execve(prog, argv, environ);
        _exit(127);
    }
    free(plan);
    free(buf);
    if (pid < 0) return -1;
    waitpid(pid, NULL, 0);
    return 0;
}
