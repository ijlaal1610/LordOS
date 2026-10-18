/* install.c — instalador de LordOS al disco (acciones del ayudante privilegiado, admin.c).
 *
 * Solo funciona en el ISO live (existe /run/live/medium); en un sistema instalado todas las
 * acciones responden ERR. Acciones:
 *
 *   install-disks           un LOG con JSON por disco instalable y al final OK <n>:
 *                           { name, path, size, model, vendor, transport, removable, parts: [...] }
 *                           (sin loop, ram, zram, sr, dm ni md, y sin el disco del que arrancó el live)
 *   install-run <json>      instala (en un proceso aparte): borra el disco, GPT con BIOS boot
 *                           (1 MiB), ESP (512 MiB, FAT32) y raíz ext4; copia el sistema del live
 *                           (unsquashfs), lo configura y le instala grub para UEFI y BIOS.
 *                           Progreso: "LOG progress <fase> <hecho> <total>", fases partition,
 *                           copy, configure, bootloader.
 *   install-reboot          reinicia (solo en el live, para terminar el asistente)
 *
 * El JSON de install-run: { disk, user, fullname, password, hostname, lang, locale, timezone,
 * kblayout, kbvariant, swap (MiB, 0 = sin swap) }. Cada campo se valida acá (root), sin
 * confiar en la interfaz: el disco tiene que estar en la lista de install-disks, los nombres
 * pasan expresiones estrictas, la región tiene que figurar en SUPPORTED, la zona en zoneinfo y
 * la distribución en xkb. La contraseña nunca va a un argv: se hashea acá con crypt(3)
 * (yescrypt, como Debian) y se escribe en /etc/shadow del sistema nuevo. Todos los programas
 * tienen ruta fija y sus argumentos los arma este archivo. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "install.h"
#include "../users/homedirs.h"

#include <crypt.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define LIVE_MEDIUM "/run/live/medium"
#define SQUASHFS    LIVE_MEDIUM "/live/filesystem.squashfs"
#define TARGET      "/run/lordos-install/target"
#define MIN_DISK    (8ull << 30)               /* 8 GiB */

static int is_live(void) { return access(SQUASHFS, R_OK) == 0; }

/* ---- utilidades ---- */

/* Copia con recorte (sin snprintf("%s"), que gcc marca con -Wformat-truncation). */
static void cpy(char *d, size_t n, const char *s) {
    size_t l = strnlen(s, n - 1);
    memcpy(d, s, l);
    d[l] = 0;
}

static int read_line(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (!f) return -1;
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f);
    out[strcspn(out, "\n")] = 0;
    size_t l = strlen(out);                     /* sin espacios al final (model de /sys) */
    while (l && out[l - 1] == ' ') out[--l] = 0;
    return 0;
}

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static int write_file(const char *path, const char *text, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.lordos-tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0) return -1;
    size_t n = strlen(text), off = 0;
    while (off < n) { ssize_t w = write(fd, text + off, n - off); if (w <= 0) { close(fd); unlink(tmp); return -1; } off += (size_t)w; }
    fchmod(fd, mode);
    if (close(fd) || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

static int writef(const char *path, mode_t mode, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int writef(const char *path, mode_t mode, const char *fmt, ...) {
    char *buf = NULL;
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(&buf, fmt, ap);
    va_end(ap);
    if (r < 0) return -1;
    r = write_file(path, buf, mode);
    free(buf);
    return r;
}

/* Disco (sda, nvme0n1…) que contiene el medio del live, o "". */
static void live_disk(char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen("/proc/self/mounts", "r");
    char dev[256], mnt[256], line[1024];
    while (f && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%255s %255s", dev, mnt) == 2 && !strcmp(mnt, LIVE_MEDIUM)) {
            char real[PATH_MAX], sys[PATH_MAX], rs[PATH_MAX];
            if (!realpath(dev, real)) break;
            const char *b = strrchr(real, '/') ? strrchr(real, '/') + 1 : real;
            int w = snprintf(sys, sizeof sys, "/sys/class/block/%s", b);
            if (w < 0 || (size_t)w >= sizeof sys || !realpath(sys, rs)) { cpy(out, n, b); break; }
            char part[PATH_MAX + 16];
            w = snprintf(part, sizeof part, "%s/partition", rs);
            if (w > 0 && (size_t)w < sizeof part && access(part, F_OK) == 0) { char *p = dirname(rs); cpy(out, n, strrchr(p, '/') + 1); }
            else cpy(out, n, b);
            break;
        }
    }
    if (f) fclose(f);
}

static int disk_name_ok(const char *d) {
    static const char *const skip[] = { "loop", "ram", "zram", "sr", "dm-", "md", "fd", "nbd", NULL };
    size_t n = strlen(d);
    if (!n || n > 31) return 0;
    for (const char *p = d; *p; p++) if (!isalnum((unsigned char)*p)) return 0;
    for (int i = 0; skip[i]; i++) if (!strncmp(d, skip[i], strlen(skip[i]))) return 0;
    return 1;
}

typedef struct { char name[32], model[96], vendor[64], transport[16]; unsigned long long size; int removable; } Disk;

/* Discos instalables, con el medio del live afuera. */
static int list_disks(Disk *out, int max) {
    char live[32];
    live_disk(live, sizeof live);
    DIR *d = opendir("/sys/block");
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) && n < max) {
        if (!disk_name_ok(e->d_name) || !strcmp(e->d_name, live)) continue;
        char p[PATH_MAX], v[128], real[PATH_MAX];
        snprintf(p, sizeof p, "/sys/block/%s/device", e->d_name);
        if (access(p, F_OK)) continue;                          /* sin dispositivo real */
        Disk *k = &out[n];
        memset(k, 0, sizeof *k);
        cpy(k->name, sizeof k->name, e->d_name);
        snprintf(p, sizeof p, "/sys/block/%s/size", e->d_name);
        read_line(p, v, sizeof v);
        k->size = strtoull(v, NULL, 10) * 512ull;
        if (!k->size) continue;
        snprintf(p, sizeof p, "/sys/block/%s/ro", e->d_name);
        read_line(p, v, sizeof v);
        if (v[0] == '1') continue;                              /* solo lectura */
        snprintf(p, sizeof p, "/sys/block/%s/removable", e->d_name);
        read_line(p, v, sizeof v);
        k->removable = v[0] == '1';
        snprintf(p, sizeof p, "/sys/block/%s/device/model", e->d_name);
        read_line(p, k->model, sizeof k->model);
        snprintf(p, sizeof p, "/sys/block/%s/device/vendor", e->d_name);
        read_line(p, k->vendor, sizeof k->vendor);
        snprintf(p, sizeof p, "/sys/block/%s", e->d_name);
        const char *t = "sata";
        if (realpath(p, real)) {
            if (strstr(real, "/usb")) t = "usb";
            else if (!strncmp(e->d_name, "nvme", 4)) t = "nvme";
            else if (!strncmp(e->d_name, "mmcblk", 6)) t = "mmc";
            else if (!strncmp(e->d_name, "vd", 2) || strstr(real, "/virtio")) t = "virtio";
        }
        cpy(k->transport, sizeof k->transport, t);
        n++;
    }
    if (d) closedir(d);
    return n;
}

static void part_path(const char *disk, int num, char *out, size_t n) {
    size_t l = strlen(disk);
    snprintf(out, n, "/dev/%s%s%d", disk, l && isdigit((unsigned char)disk[l - 1]) ? "p" : "", num);
}

/* ---- install-disks ---- */

/* Valor de KEY="…" en una línea de lsblk -P. */
static void pair(const char *line, const char *key, char *out, size_t n) {
    out[0] = 0;
    char k[40];
    snprintf(k, sizeof k, "%s=\"", key);
    const char *p = strstr(line, k);
    if (p && (p == line || p[-1] == ' ')) {
        p += strlen(k);
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
        out[i] = 0;
    }
}

void install_act_disks(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)arg; (void)len;
    if (!is_live()) { admin_say("ERR", "el instalador solo funciona desde el sistema en vivo"); return; }
    Disk disks[32];
    int n = list_disks(disks, 32);
    for (int i = 0; i < n; i++) {
        Disk *k = &disks[i];
        char dev[48], out[16384];
        int w = snprintf(dev, sizeof dev, "/dev/%s", k->name);
        if (w < 0 || (size_t)w >= sizeof dev) continue;
        const char *const argv[] = { "/usr/bin/lsblk", "-n", "-P", "-b", "-o", "NAME,TYPE,FSTYPE,LABEL,SIZE", dev, NULL };
        if (admin_run_capture(argv, out, sizeof out) != 0) out[0] = 0;
        char *js = NULL;
        size_t jl = 0;
        FILE *f = open_memstream(&js, &jl);
        if (!f) continue;
        fprintf(f, "{\"name\":"); json_str(f, k->name);
        fprintf(f, ",\"path\":"); json_str(f, dev);
        fprintf(f, ",\"size\":%llu,\"model\":", k->size); json_str(f, k->model);
        fprintf(f, ",\"vendor\":"); json_str(f, k->vendor);
        fprintf(f, ",\"transport\":"); json_str(f, k->transport);
        fprintf(f, ",\"removable\":%s,\"usable\":%s,\"parts\":[", k->removable ? "true" : "false", k->size >= MIN_DISK ? "true" : "false");
        int first = 1;
        for (char *l = out, *next; l && *l; l = next) {
            next = strchr(l, '\n');
            if (next) *next++ = 0;
            char name[64], type[16], fs[32], label[64], size[32];
            pair(l, "NAME", name, sizeof name); pair(l, "TYPE", type, sizeof type);
            pair(l, "FSTYPE", fs, sizeof fs); pair(l, "LABEL", label, sizeof label); pair(l, "SIZE", size, sizeof size);
            if (strcmp(type, "part")) continue;
            fprintf(f, "%s{\"name\":", first ? "" : ","); json_str(f, name);
            fprintf(f, ",\"fstype\":"); json_str(f, fs);
            fprintf(f, ",\"label\":"); json_str(f, label);
            fprintf(f, ",\"size\":%llu}", strtoull(size, NULL, 10));
            first = 0;
        }
        fprintf(f, "]}");
        fclose(f);
        admin_say("LOG", "%s", js);
        free(js);
    }
    admin_say("OK", "%d", n);
}

/* ---- JSON plano de install-run ---- */

typedef struct { char disk[32], user[33], fullname[128], password[257], hostname[64], lang[16], locale[64], timezone[64], kblayout[24], kbvariant[40]; long swap; } Plan;

/* Lee un string JSON (con escapes y \uXXXX → UTF-8). Devuelve el puntero después de la comilla. */
static const char *jstring(const char *p, char *out, size_t n, int *ok) {
    size_t o = 0;
    if (*p != '"') { *ok = 0; return p; }
    for (p++; *p && *p != '"'; p++) {
        unsigned cp = (unsigned char)*p;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': cp = '\n'; break; case 't': cp = '\t'; break; case 'r': cp = '\r'; break;
            case 'b': cp = '\b'; break; case 'f': cp = '\f'; break;
            case 'u': {
                char h[5] = { 0 };
                for (int i = 0; i < 4; i++) { if (!isxdigit((unsigned char)p[1 + i])) { *ok = 0; return p; } h[i] = p[1 + i]; }
                cp = (unsigned)strtoul(h, NULL, 16);
                p += 4;
                if (cp >= 0xd800 && cp < 0xe000) { *ok = 0; return p; }      /* sin pares sustitutos */
                break;
            }
            case 0: *ok = 0; return p;
            default: cp = (unsigned char)*p;
            }
        }
        char u[4];
        int ul = 1;
        if (cp < 0x80 || (unsigned char)p[0] >= 0x80) u[0] = (char)cp;          /* bytes UTF-8 tal cual */
        else if (cp < 0x800) { u[0] = (char)(0xc0 | cp >> 6); u[1] = (char)(0x80 | (cp & 63)); ul = 2; }
        else { u[0] = (char)(0xe0 | cp >> 12); u[1] = (char)(0x80 | ((cp >> 6) & 63)); u[2] = (char)(0x80 | (cp & 63)); ul = 3; }
        if (o + (size_t)ul >= n) { *ok = 0; return p; }
        memcpy(out + o, u, (size_t)ul);
        o += (size_t)ul;
    }
    if (*p != '"') { *ok = 0; return p; }
    out[o] = 0;
    return p + 1;
}

static int parse_plan(const char *s, Plan *pl) {
    memset(pl, 0, sizeof *pl);
    int ok = 1;
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    if (*p++ != '{') return -1;
    while (ok) {
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (*p == '}') break;
        char key[32];
        p = jstring(p, key, sizeof key, &ok);
        while (isspace((unsigned char)*p)) p++;
        if (!ok || *p++ != ':') return -1;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '"') {
            char val[300];
            p = jstring(p, val, sizeof val, &ok);
            if (!ok) return -1;
            struct { const char *k; char *dst; size_t n; } f[] = {
                { "disk", pl->disk, sizeof pl->disk }, { "user", pl->user, sizeof pl->user },
                { "fullname", pl->fullname, sizeof pl->fullname }, { "password", pl->password, sizeof pl->password },
                { "hostname", pl->hostname, sizeof pl->hostname }, { "lang", pl->lang, sizeof pl->lang },
                { "locale", pl->locale, sizeof pl->locale }, { "timezone", pl->timezone, sizeof pl->timezone },
                { "kblayout", pl->kblayout, sizeof pl->kblayout }, { "kbvariant", pl->kbvariant, sizeof pl->kbvariant },
            };
            for (size_t i = 0; i < sizeof f / sizeof *f; i++)
                if (!strcmp(key, f[i].k)) { if (strlen(val) >= f[i].n) return -1; cpy(f[i].dst, f[i].n, val); }
        } else if (isdigit((unsigned char)*p) || *p == '-') {
            char *end;
            long v = strtol(p, &end, 10);
            if (!strcmp(key, "swap")) pl->swap = v;
            p = end;
        } else if (!strncmp(p, "true", 4) || !strncmp(p, "null", 4)) p += 4;
        else if (!strncmp(p, "false", 5)) p += 5;
        else return -1;
    }
    return ok ? 0 : -1;
}

static int re_user(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 32 || !(islower((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (; *s; s++) if (!islower((unsigned char)*s) && !isdigit((unsigned char)*s) && *s != '_' && *s != '-') return 0;
    return 1;
}

static int re_host(const char *s) {
    size_t n = strlen(s);
    if (!n || n > 63 || s[0] == '-' || s[n - 1] == '-') return 0;
    for (; *s; s++) if (!islower((unsigned char)*s) && !isdigit((unsigned char)*s) && *s != '-') return 0;
    return 1;
}

static int re_simple(const char *s, size_t max, const char *extra, int allow_empty) {
    size_t n = strlen(s);
    if ((!n && !allow_empty) || n > max) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr(extra, *s)) return 0;
    return 1;
}

static int locale_supported(const char *name) {
    if (!re_simple(name, 48, "_.-@", 0)) return 0;
    FILE *f = fopen("/usr/share/i18n/SUPPORTED", "r");
    char line[256], loc[128], cs[64];
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f))
        if (sscanf(line, "%127s %63s", loc, cs) == 2 && !strcmp(loc, name) && !strcmp(cs, "UTF-8")) found = 1;
    if (f) fclose(f);
    return found;
}

static const char *validate(Plan *pl) {
    Disk disks[32];
    int n = list_disks(disks, 32), found = 0;
    for (int i = 0; i < n; i++) if (!strcmp(disks[i].name, pl->disk)) { found = 1; if (disks[i].size < MIN_DISK) return "el disco es demasiado chico (mínimo 8 GB)"; }
    if (!found) return "el disco no está en la lista de discos instalables";
    static const char *const reserved[] = { "root", "daemon", "bin", "sys", "sync", "games", "man", "lp", "mail", "news", "uucp",
        "proxy", "www-data", "backup", "list", "irc", "nobody", "systemd-network", "messagebus", "pipewire", "sudo", "admin", NULL };
    if (!re_user(pl->user)) return "el nombre de usuario es inválido (minúsculas, números, - y _)";
    for (int i = 0; reserved[i]; i++) if (!strcmp(pl->user, reserved[i])) return "ese nombre de usuario está reservado";
    struct passwd *pw = getpwnam(pl->user);
    if (pw && strcmp(pl->user, "lordos")) return "ese nombre de usuario ya lo usa el sistema";
    for (const unsigned char *p = (const unsigned char *)pl->fullname; *p; p++) if (*p < 0x20 || *p == ':' || *p == ',' || *p == 0x7f) return "el nombre completo tiene caracteres inválidos";
    if (!pl->password[0]) return "falta la contraseña";
    if (!re_host(pl->hostname)) return "el nombre del equipo es inválido (minúsculas, números y -)";
    if (!re_simple(pl->lang, 10, "-", 0)) return "idioma inválido";
    if (!locale_supported(pl->locale)) return "región no soportada";
    char p[PATH_MAX];
    struct stat st;
    if (!re_simple(pl->timezone, 60, "/_+-", 0) || strstr(pl->timezone, "..") || pl->timezone[0] == '/') return "zona horaria inválida";
    snprintf(p, sizeof p, "/usr/share/zoneinfo/%s", pl->timezone);
    if (stat(p, &st) || !S_ISREG(st.st_mode)) return "zona horaria desconocida";
    if (!re_simple(pl->kblayout, 20, "_", 0)) return "distribución de teclado inválida";
    snprintf(p, sizeof p, "/usr/share/X11/xkb/symbols/%s", pl->kblayout);
    if (access(p, R_OK)) return "distribución de teclado desconocida";
    if (!re_simple(pl->kbvariant, 36, "_-", 1)) return "variante de teclado inválida";
    if (pl->swap < 0 || pl->swap > 16384) return "tamaño de swap inválido (0 a 16384 MB)";
    return NULL;
}

/* ---- pasos de install-run ---- */

static void progress(const char *phase, int done, int total) { admin_say("LOG", "progress %s %d %d", phase, done, total); }

static int run(const char *const argv[]) {
    int r = admin_run(argv);
    if (r) admin_say("LOG", "falló %s (código %d)", argv[0], r);
    return r;
}

/* Programa dentro del sistema nuevo: chroot TARGET argv... */
static int in_target(const char *const argv[]) {
    const char *full[24] = { "/usr/sbin/chroot", TARGET };
    int i = 0;
    for (; argv[i] && i < 21; i++) full[2 + i] = argv[i];
    full[2 + i] = NULL;
    return run(full);
}

/* unsquashfs con su porcentaje como progreso de la fase copy. */
static int copy_system(void) {
    const char *const argv[] = { "/usr/bin/unsquashfs", "-f", "-percentage", "-d", TARGET, SQUASHFS, NULL };
    int p[2];
    if (pipe2(p, O_CLOEXEC)) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0); dup2(p[1], 1); dup2(p[1], 2);
        syscall(SYS_close_range, 3, ~0u, 0);
        char *const env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C.UTF-8", NULL };
        execve(argv[0], (char *const *)argv, env);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return -1; }
    char buf[256];
    int last = -1, cur = 0, any = 0;
    ssize_t n;
    while ((n = read(p[0], buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < n; i++) {
            if (isdigit((unsigned char)buf[i])) { cur = cur * 10 + (buf[i] - '0'); any = 1; }
            else { if (any && cur <= 100 && cur != last) { progress("copy", cur, 100); last = cur; } cur = 0; any = 0; }
        }
    close(p[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}

static int uuid_of(const char *dev, char *out, size_t n) {
    const char *const argv[] = { "/usr/sbin/blkid", "-s", "UUID", "-o", "value", dev, NULL };
    char buf[128];
    if (admin_run_capture(argv, buf, sizeof buf)) return -1;
    buf[strcspn(buf, "\n")] = 0;
    if (!re_simple(buf, 64, "-", 0)) return -1;
    cpy(out, n, buf);
    return 0;
}

/* Cambia el hash de un usuario en etc/shadow del sistema nuevo (sin pasar por argv). */
static int set_shadow(const char *user, const char *hash) {
    const char *path = TARGET "/etc/shadow";
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *out = NULL;
    size_t ol = 0;
    FILE *m = open_memstream(&out, &ol);
    char line[1024];
    size_t ul = strlen(user);
    int done = 0;
    while (m && fgets(line, sizeof line, f)) {
        char *c = strchr(line, ':');
        if (c && (size_t)(c - line) == ul && !strncmp(line, user, ul)) {
            char *rest = strchr(c + 1, ':');
            fprintf(m, "%s:%s%s", user, hash, rest ? rest : ":\n");
            done = 1;
        } else fputs(line, m);
    }
    fclose(f);
    if (!m) return -1;
    fclose(m);
    struct stat st;
    stat(path, &st);
    int r = done ? write_file(path, out, st.st_mode & 07777) : -1;
    if (!r) if (chown(path, st.st_uid, st.st_gid)) admin_say("LOG", "aviso: no se pudo cambiar el dueño");
    explicit_bzero(out, ol);
    free(out);
    return r;
}

/* uid y gid del usuario en etc/passwd del sistema nuevo. */
static int target_ids(const char *user, uid_t *uid, gid_t *gid) {
    FILE *f = fopen(TARGET "/etc/passwd", "r");
    char line[1024];
    int ok = -1;
    size_t ul = strlen(user);
    while (f && ok && fgets(line, sizeof line, f)) {
        if (strncmp(line, user, ul) || line[ul] != ':') continue;
        unsigned u, g;
        if (sscanf(line + ul, ":%*[^:]:%u:%u:", &u, &g) == 2) { *uid = u; *gid = g; ok = 0; }
    }
    if (f) fclose(f);
    return ok;
}

static void umount_target(void) {
    static const char *const sub[] = { "/sys/firmware/efi/efivars", "/run", "/sys", "/proc", "/dev/pts", "/dev", "/boot/efi", "", NULL };
    sync();
    for (int i = 0; sub[i]; i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, TARGET "%s", sub[i]);
        umount2(p, MNT_DETACH);
    }
}

static int bind(const char *what) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, TARGET "%s", what);
    mkdir(p, 0755);
    if (mount(what, p, NULL, MS_BIND | MS_REC, NULL)) return -1;
    /* esclavo: lo que se desmonte acá adentro no se propaga al sistema en vivo (en systemd /run,
     * /dev y /sys son "shared": desmontar TARGET/run desmontaba /run/live/medium) */
    return mount(NULL, p, NULL, MS_REC | MS_SLAVE, NULL);
}

static int disk_busy(const char *disk) {
    char dev[48], line[1024];
    snprintf(dev, sizeof dev, "/dev/%s", disk);
    size_t dl = strlen(dev);
    int busy = 0;
    FILE *f = fopen("/proc/self/mounts", "r");
    while (f && !busy && fgets(line, sizeof line, f)) if (!strncmp(line, dev, dl)) busy = 1;
    if (f) fclose(f);
    f = fopen("/proc/swaps", "r");
    while (f && !busy && fgets(line, sizeof line, f)) if (!strncmp(line, dev, dl)) busy = 1;
    if (f) fclose(f);
    return busy;
}

#define FAIL(...) do { admin_say("ERR", __VA_ARGS__); umount_target(); explicit_bzero(&pl, sizeof pl); return; } while (0)

void install_act_run(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)len;
    Plan pl;
    if (!is_live()) { admin_say("ERR", "el instalador solo funciona desde el sistema en vivo"); return; }
    if (parse_plan(arg, &pl)) { admin_say("ERR", "datos de instalación inválidos"); return; }
    const char *why = validate(&pl);
    if (why) { explicit_bzero(&pl, sizeof pl); admin_say("ERR", "%s", why); return; }
    if (disk_busy(pl.disk)) { admin_say("ERR", "el disco /dev/%s está en uso (montado o con swap)", pl.disk); explicit_bzero(&pl, sizeof pl); return; }

    char dev[48], bios[48], esp[48], root[48];
    snprintf(dev, sizeof dev, "/dev/%s", pl.disk);
    part_path(pl.disk, 1, bios, sizeof bios);
    part_path(pl.disk, 2, esp, sizeof esp);
    part_path(pl.disk, 3, root, sizeof root);
    admin_say("LOG", "instalando en %s", dev);

    /* 1. particionar: GPT con BIOS boot (1 MiB), ESP (512 MiB) y raíz */
    progress("partition", 0, 4);
    { const char *const a[] = { "/usr/sbin/wipefs", "-a", dev, NULL }; if (run(a)) FAIL("no se pudo borrar el disco"); }
    { const char *const a[] = { "/usr/sbin/sgdisk", "--zap-all", dev, NULL }; run(a); }
    { const char *const a[] = { "/usr/sbin/sgdisk", "-n", "1:0:+1M", "-t", "1:ef02", "-c", "1:biosboot",
                                "-n", "2:0:+512M", "-t", "2:ef00", "-c", "2:EFI",
                                "-n", "3:0:0", "-t", "3:8304", "-c", "3:LordOS", dev, NULL };
      if (run(a)) FAIL("no se pudo particionar el disco"); }
    progress("partition", 1, 4);
    { const char *const a[] = { "/usr/bin/udevadm", "settle", "--timeout=10", NULL }; run(a); }
    for (int i = 0; i < 50 && (access(esp, F_OK) || access(root, F_OK)); i++) usleep(200000);
    if (access(esp, F_OK) || access(root, F_OK)) FAIL("no aparecieron las particiones nuevas");
    { const char *const a[] = { "/usr/sbin/mkfs.vfat", "-F", "32", "-n", "EFI", esp, NULL }; if (run(a)) FAIL("no se pudo formatear la partición EFI"); }
    progress("partition", 2, 4);
    { const char *const a[] = { "/usr/sbin/mkfs.ext4", "-F", "-q", "-L", "LordOS", root, NULL }; if (run(a)) FAIL("no se pudo formatear la partición del sistema"); }
    progress("partition", 3, 4);
    mkdir("/run/lordos-install", 0700);
    mkdir(TARGET, 0755);
    umount_target();
    if (mount(root, TARGET, "ext4", 0, NULL)) FAIL("no se pudo montar %s: %s", root, strerror(errno));
    progress("partition", 4, 4);

    /* 2. copiar el sistema del live */
    progress("copy", 0, 100);
    if (copy_system()) FAIL("no se pudo copiar el sistema");
    progress("copy", 100, 100);
    mkdir(TARGET "/boot/efi", 0755);
    if (mount(esp, TARGET "/boot/efi", "vfat", 0, "umask=0077")) FAIL("no se pudo montar la partición EFI");
    if (bind("/dev") || bind("/proc") || bind("/sys") || bind("/run")) FAIL("no se pudo preparar el sistema nuevo");

    /* 3. configurar */
    const int steps = 9;
    int step = 0;
    progress("configure", step, steps);
    char ru[80], eu[80];
    if (uuid_of(root, ru, sizeof ru) || uuid_of(esp, eu, sizeof eu)) FAIL("no se pudo leer el UUID de las particiones");
    char swapline[64] = "";
    if (pl.swap > 0) snprintf(swapline, sizeof swapline, "/swapfile none swap sw 0 0\n");
    if (writef(TARGET "/etc/fstab", 0644,
               "# /etc/fstab — generado por el instalador de LordOS\n"
               "UUID=%s / ext4 errors=remount-ro 0 1\nUUID=%s /boot/efi vfat umask=0077 0 1\n%s", ru, eu, swapline))
        FAIL("no se pudo escribir /etc/fstab");
    writef(TARGET "/etc/hostname", 0644, "%s\n", pl.hostname);
    writef(TARGET "/etc/hosts", 0644, "127.0.0.1\tlocalhost\n127.0.1.1\t%s\n::1\tlocalhost ip6-localhost ip6-loopback\n", pl.hostname);
    progress("configure", ++step, steps);

    /* sin lo del live: live-boot, el usuario de prueba y su sudo sin contraseña */
    unlink(TARGET "/etc/sudoers.d/lordos-live");
    unlink(TARGET "/var/lib/systemd/linger/lordos");
    unlink(TARGET "/etc/sysctl.d/50-lordos-ping.conf.dpkg-old");
    { const char *const a[] = { "/usr/bin/dpkg", "--purge", "live-boot", "live-boot-initramfs-tools", NULL }; in_target(a); }
    { const char *const a[] = { "/usr/sbin/userdel", "-r", "lordos", NULL }; in_target(a); }
    progress("configure", ++step, steps);

    /* el usuario, con su contraseña hasheada acá (yescrypt) */
    { const char *const a[] = { "/usr/sbin/useradd", "-m", "-s", "/bin/bash", "-c", pl.fullname, "-G", "sudo,audio,video,input,render,pipewire", pl.user, NULL };
      if (in_target(a)) FAIL("no se pudo crear el usuario"); }
    /* administrador de impresoras de CUPS (grupo lpadmin, docs/IMPRESION.md); si CUPS no vino en
     * la imagen, el grupo no existe y no pasa nada */
    { const char *const a[] = { "/usr/sbin/usermod", "--append", "--groups", "lpadmin", pl.user, NULL }; if (in_target(a)) { /* sin CUPS */ } }
    char *salt = crypt_gensalt_ra("$y$", 0, NULL, 0);
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    const char *hash = salt ? crypt_r(pl.password, salt, &cd) : NULL;
    free(salt);
    if (!hash || hash[0] == '*') FAIL("no se pudo cifrar la contraseña");
    if (set_shadow(pl.user, hash) || set_shadow("root", "*")) { explicit_bzero(&cd, sizeof cd); FAIL("no se pudo guardar la contraseña"); }
    explicit_bzero(&cd, sizeof cd);
    explicit_bzero(pl.password, sizeof pl.password);
    mkdir(TARGET "/var/lib/systemd/linger", 0755);
    { char p[PATH_MAX]; snprintf(p, sizeof p, TARGET "/var/lib/systemd/linger/%s", pl.user); write_file(p, "", 0644); }
    mkdir(TARGET "/etc/lordos", 0755);
    /* lord-sessiond (docs/SESIONES.md): el usuario entra por la pantalla de ingreso; el inicio
     * automático se prende en Ajustes → Usuarios (AUTOLOGIN=). USER= es para el lordos-session
     * de antes de M8. */
    writef(TARGET "/etc/lordos/session.conf", 0644, "# Lo escribe el instalador de LordOS. Ver docs/SESIONES.md\n"
           "# AUTOLOGIN=usuario entra solo al arrancar; vacío: pantalla de ingreso\nAUTOLOGIN=\nUSER=%s\n", pl.user);
    progress("configure", ++step, steps);

    /* idioma, región, zona horaria y teclado */
    { char p[PATH_MAX], line[256]; snprintf(p, sizeof p, TARGET "/etc/locale.gen");
      FILE *f = fopen(p, "r"); int has = 0; size_t ll = strlen(pl.locale);
      while (f && fgets(line, sizeof line, f)) if (!strncmp(line, pl.locale, ll) && line[ll] == ' ') has = 1;
      if (f) fclose(f);
      if (!has) { f = fopen(p, "a"); if (f) { fprintf(f, "%s UTF-8\n", pl.locale); fclose(f); }
                  const char *const a[] = { "/usr/sbin/locale-gen", NULL }; in_target(a); } }
    writef(TARGET "/etc/default/locale", 0644, "LANG=%s\n", pl.locale);
    { char zp[PATH_MAX]; snprintf(zp, sizeof zp, "/usr/share/zoneinfo/%s", pl.timezone);
      unlink(TARGET "/etc/localtime"); if (symlink(zp, TARGET "/etc/localtime")) admin_say("LOG", "aviso: /etc/localtime");
      writef(TARGET "/etc/timezone", 0644, "%s\n", pl.timezone); }
    writef(TARGET "/etc/default/keyboard", 0644, "XKBMODEL=\"pc105\"\nXKBLAYOUT=\"%s\"\nXKBVARIANT=\"%s\"\nXKBOPTIONS=\"\"\nBACKSPACE=\"guess\"\n", pl.kblayout, pl.kbvariant);
    progress("configure", ++step, steps);

    /* ajustes de LordOS del usuario: idioma y teclado elegidos */
    uid_t uid; gid_t gid;
    if (target_ids(pl.user, &uid, &gid)) FAIL("no se encontró el usuario nuevo");
    { char p[PATH_MAX];
      snprintf(p, sizeof p, TARGET "/home/%s/.config", pl.user); mkdir(p, 0755); if (chown(p, uid, gid)) admin_say("LOG", "aviso: no se pudo cambiar el dueño");
      snprintf(p, sizeof p, TARGET "/home/%s/.config/lordos", pl.user); mkdir(p, 0755); if (chown(p, uid, gid)) admin_say("LOG", "aviso: no se pudo cambiar el dueño");
      snprintf(p, sizeof p, TARGET "/home/%s/.config/lordos/settings.json", pl.user);
      writef(p, 0644, "{\n  \"language\": \"%s\",\n  \"keyboards\": [{ \"layout\": \"%s\", \"variant\": \"%s\" }],\n  \"keyboardIndex\": 0\n}\n", pl.lang, pl.kblayout, pl.kbvariant);
      if (chown(p, uid, gid)) admin_say("LOG", "aviso: no se pudo cambiar el dueño");
      snprintf(p, sizeof p, TARGET "/home/%s/.config/locale.conf", pl.user);
      writef(p, 0644, "LANG=%s\n", pl.locale); if (chown(p, uid, gid)) admin_say("LOG", "aviso: no se pudo cambiar el dueño"); }
    /* Escritorio, Documentos, Descargas… en el idioma elegido y ~/.config/user-dirs.dirs (la misma
     * lista que Ajustes → Usuarios y el live: users/homedirs.c) */
    { char p[PATH_MAX]; snprintf(p, sizeof p, TARGET "/home/%s", pl.user);
      if (homedirs_create(p, uid, gid, pl.locale)) admin_say("LOG", "aviso: no se pudieron crear las carpetas del usuario"); }
    progress("configure", ++step, steps);

    /* swap como archivo */
    if (pl.swap > 0) {
        char sz[24]; snprintf(sz, sizeof sz, "%ldM", pl.swap);
        const char *const a[] = { "/usr/bin/fallocate", "-l", sz, TARGET "/swapfile", NULL };
        if (!run(a)) { chmod(TARGET "/swapfile", 0600); const char *const b[] = { "/usr/sbin/mkswap", "-q", TARGET "/swapfile", NULL }; run(b); }
    }
    progress("configure", ++step, steps);

    /* identidad de la máquina y arranque */
    unlink(TARGET "/etc/machine-id");
    { const char *const a[] = { "/usr/bin/systemd-machine-id-setup", NULL }; in_target(a); }
    writef(TARGET "/etc/default/grub", 0644,
           "# generado por el instalador de LordOS\nGRUB_DEFAULT=0\nGRUB_TIMEOUT=2\nGRUB_DISTRIBUTOR=LordOS\n"
           "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet loglevel=3\"\nGRUB_CMDLINE_LINUX=\"\"\nGRUB_DISABLE_OS_PROBER=true\n");
    progress("configure", ++step, steps);
    { const char *const a[] = { "/usr/sbin/update-initramfs", "-u", "-k", "all", NULL }; if (in_target(a)) FAIL("no se pudo generar el initramfs"); }
    progress("configure", ++step, steps);
    progress("configure", steps, steps);

    /* 4. arranque: grub para UEFI (ruta removible, sirve en cualquier firmware) y para BIOS */
    progress("bootloader", 0, 3);
    { const char *const a[] = { "/usr/sbin/grub-install", "--target=x86_64-efi", "--efi-directory=/boot/efi", "--removable", "--no-nvram", NULL };
      if (in_target(a)) FAIL("no se pudo instalar grub (UEFI)"); }
    progress("bootloader", 1, 3);
    { const char *const a[] = { "/usr/sbin/grub-install", "--target=i386-pc", dev, NULL };
      if (in_target(a)) FAIL("no se pudo instalar grub (BIOS)"); }
    progress("bootloader", 2, 3);
    { const char *const a[] = { "/usr/sbin/grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL };
      if (in_target(a)) FAIL("no se pudo generar la configuración de grub"); }
    progress("bootloader", 3, 3);
    umount_target();
    explicit_bzero(&pl, sizeof pl);
    admin_say("OK", "LordOS quedó instalado en %s", dev);
}

/* Reinicia sin quedarse esperando: un proceso aparte (sobrevive al ayudante, que muere con
 * lord-server al apagarse la sesión) pide el reinicio a systemd sin bloquear (--no-block) y, si
 * a los 20 s el sistema sigue vivo y nadie lo terminó, hace sync + reboot(2). */
void install_act_reboot(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)arg; (void)len;
    if (!is_live() && access("/run/live", F_OK)) { admin_say("ERR", "solo desde el sistema en vivo"); return; }
    sync();
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        signal(SIGHUP, SIG_IGN);            /* SIGTERM queda normal: si systemd lo manda, el apagado ya está en marcha */
        prctl(PR_SET_NAME, "lordos-reboot");
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
        syscall(SYS_close_range, 3, ~0u, 0);
        pid_t c = fork();
        if (c == 0) {
            char *const argv[] = { "/usr/bin/systemctl", "--no-block", "reboot", NULL };
            char *const env[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
            execve(argv[0], argv, env);
            _exit(127);
        }
        if (c > 0) waitpid(c, NULL, 0);
        sleep(20);
        sync();
        reboot(RB_AUTOBOOT);
        _exit(0);
    }
    if (pid < 0) { admin_say("ERR", "no se pudo reiniciar"); return; }
    waitpid(pid, NULL, 0);
    admin_say("OK", "reiniciando");
}
