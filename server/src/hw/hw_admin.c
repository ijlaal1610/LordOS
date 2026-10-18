/* hw_admin.c — Settings' admin actions (see hw_admin.h). Runs as root in the admin helper. */
#include "hw_admin.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/rfkill.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One argument line, trimmed, into out. 0 if it fits. */
static int arg_copy(const char *arg, size_t len, char *out, size_t n) {
    while (len && (arg[len - 1] == '\n' || arg[len - 1] == ' ')) len--;
    if (len >= n || memchr(arg, '\n', len) || memchr(arg, 0, len)) return -1;
    memcpy(out, arg, len);
    out[len] = 0;
    return 0;
}

static int write_file(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    ssize_t w = write(fd, text, strlen(text));
    int e = errno;
    close(fd);
    errno = e;
    return w == (ssize_t)strlen(text) ? 0 : -1;
}

static long read_long(const char *path) {
    char b[32];
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    ssize_t r = read(fd, b, sizeof b - 1);
    close(fd);
    if (r <= 0) return -1;
    b[r] = 0;
    char *e;
    long v = strtol(b, &e, 10);
    return (*e == 0 || *e == '\n') ? v : -1;
}

/* ---- backlight ---- */

int hw_backlight_name_valid(const char *n) {
    size_t l = n ? strlen(n) : 0;
    if (!l || l > 64 || n[0] == '.') return 0;
    for (const char *p = n; *p; p++) if (!(isalnum((unsigned char)*p) || strchr("_-.:", *p))) return 0;
    return 1;
}

void hw_act_brightness(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char a[128], name[72];
    long v;
    if (arg_copy(arg, len, a, sizeof a) || sscanf(a, "%71s %ld", name, &v) != 2 || !hw_backlight_name_valid(name)) {
        admin_say("ERR", "invalid argument");
        return;
    }
    /* the entry has to be a real backlight device under /sys/devices */
    char link[PATH_MAX], real[PATH_MAX];
    snprintf(link, sizeof link, "/sys/class/backlight/%s", name);
    if (!realpath(link, real) || strncmp(real, "/sys/devices/", 13)) { admin_say("ERR", "no such backlight"); return; }
    char p[PATH_MAX + 32];
    snprintf(p, sizeof p, "%s/max_brightness", real);
    long max = read_long(p);
    if (max <= 0 || v < 0 || v > max) { admin_say("ERR", "value out of range (0..%ld)", max); return; }
    char val[32];
    snprintf(val, sizeof val, "%ld\n", v);
    snprintf(p, sizeof p, "%s/brightness", real);
    if (write_file(p, val)) admin_say("ERR", "could not set the brightness: %s", strerror(errno));
    else admin_say("OK", "%ld", v);
}

/* ---- power profile ---- */

static int file_ok(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 022);
}

void hw_act_power_profile(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char p[24];
    if (arg_copy(arg, len, p, sizeof p) || (strcmp(p, "performance") && strcmp(p, "balanced") && strcmp(p, "power-saver"))) {
        admin_say("ERR", "invalid profile");
        return;
    }
    if (file_ok("/usr/bin/powerprofilesctl")) {
        const char *const argv[] = { "/usr/bin/powerprofilesctl", "set", p, NULL };
        int code = admin_run(argv);
        if (code) admin_say("ERR", "powerprofilesctl failed (%d)", code);
        else admin_say("OK", "%s", p);
        return;
    }
    /* no daemon: the cpufreq governor of every policy (and the energy/performance preference) */
    DIR *d = opendir("/sys/devices/system/cpu/cpufreq");
    if (!d) { admin_say("ERR", "this computer has no CPU frequency scaling"); return; }
    int done = 0, failed = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (strncmp(e->d_name, "policy", 6) || !isdigit((unsigned char)e->d_name[6])) continue;
        char path[400], avail[512] = "";
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpufreq/%s/scaling_available_governors", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) { ssize_t n = read(fd, avail, sizeof avail - 1); if (n > 0) avail[n] = 0; close(fd); }
        const char *want = !strcmp(p, "performance") ? "performance" : !strcmp(p, "power-saver") ? "powersave"
                         : strstr(avail, "schedutil") ? "schedutil" : strstr(avail, "ondemand") ? "ondemand" : "powersave";
        if (!strstr(avail, want)) { failed++; continue; }
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", e->d_name);
        char line[32];
        snprintf(line, sizeof line, "%s\n", want);
        if (write_file(path, line)) { failed++; continue; }
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpufreq/%s/energy_performance_preference", e->d_name);
        const char *epp = !strcmp(p, "performance") ? "performance\n" : !strcmp(p, "power-saver") ? "power\n" : "balance_performance\n";
        if (access(path, F_OK) == 0) write_file(path, epp);         /* only on intel_pstate/amd-pstate */
        done++;
    }
    closedir(d);
    if (!done) admin_say("ERR", "no CPU policy accepted the profile");
    else admin_say("OK", "%s (%d policies%s)", p, done, failed ? ", some refused" : "");
}

void hw_act_suspend(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)arg; (void)len;
    const char *const argv[] = { "/usr/bin/systemctl", "suspend", NULL };
    int code = admin_run(argv);
    if (code) admin_say("ERR", "systemctl suspend failed (%d)", code);
    else admin_say("OK", "suspend");
}

/* ---- power button and lid (logind) ---- */

/* The same short list for the power button and the lid (lid = 1): what logind can do without a
 * session lock of its own (LordOS locks from the theme). */
int hw_button_action_valid(const char *a, int lid) {
    static const char *const ok[] = { "poweroff", "suspend", "hibernate", "ignore", NULL };
    (void)lid;
    for (int i = 0; a && ok[i]; i++) if (!strcmp(a, ok[i])) return 1;
    return 0;
}

void hw_act_buttons(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char a[128], power[16], lid[16], lidac[16];
    if (arg_copy(arg, len, a, sizeof a) || sscanf(a, "%15s %15s %15s", power, lid, lidac) != 3 ||
        !hw_button_action_valid(power, 0) || !hw_button_action_valid(lid, 1) || !hw_button_action_valid(lidac, 1)) {
        admin_say("ERR", "invalid argument");
        return;
    }
    mkdir("/etc/systemd/logind.conf.d", 0755);
    char text[400];
    snprintf(text, sizeof text,
             "# Written by LordOS (Settings → Battery). Changes here are overwritten.\n"
             "[Login]\nHandlePowerKey=%s\nHandleLidSwitch=%s\nHandleLidSwitchExternalPower=%s\n", power, lid, lidac);
    const char *tmp = "/etc/systemd/logind.conf.d/.lordos.conf.tmp", *dst = "/etc/systemd/logind.conf.d/lordos.conf";
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    int ok = fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    if (fd >= 0) { ok = fsync(fd) == 0 && ok; close(fd); }
    if (!ok || rename(tmp, dst)) { unlink(tmp); admin_say("ERR", "could not write %s", dst); return; }
    const char *const argv[] = { "/usr/bin/systemctl", "reload", "systemd-logind", NULL };
    int code = admin_run(argv);
    if (code) admin_say("ERR", "logind did not reload (%d)", code);
    else admin_say("OK", "%s %s %s", power, lid, lidac);
}

/* ---- date and time ---- */

void hw_act_ntp(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char a[8];
    if (arg_copy(arg, len, a, sizeof a) || (strcmp(a, "on") && strcmp(a, "off"))) { admin_say("ERR", "invalid argument"); return; }
    const char *const argv[] = { "/usr/bin/timedatectl", "set-ntp", !strcmp(a, "on") ? "true" : "false", NULL };
    int code = admin_run(argv);
    if (code) admin_say("ERR", "timedatectl set-ntp failed (%d)", code);
    else admin_say("OK", "%s", a);
}

int hw_time_valid(const char *t) {
    if (!t || strlen(t) != 19) return 0;
    for (int i = 0; i < 19; i++) {
        char c = t[i];
        int sep = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : 0;
        if (sep ? c != sep : !isdigit((unsigned char)c)) return 0;
    }
    int y, mo, d, h, mi, s;
    if (sscanf(t, "%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > days[mo - 1] + (mo == 2 && leap)) return 0;
    return h <= 23 && mi <= 59 && s <= 59;
}

void hw_act_time_set(const AdminReq *r, const char *arg, size_t len) {
    (void)r;
    char t[32];
    if (arg_copy(arg, len, t, sizeof t) || !hw_time_valid(t)) { admin_say("ERR", "invalid date and time"); return; }
    const char *const argv[] = { "/usr/bin/timedatectl", "set-time", t, NULL };
    int code = admin_run(argv);
    if (code) admin_say("ERR", "timedatectl set-time failed (%d): turn off automatic time first", code);
    else admin_say("OK", "%s", t);
}

/* ---- Bluetooth rfkill ---- */

void hw_act_bt_unblock(const AdminReq *r, const char *arg, size_t len) {
    (void)r; (void)arg; (void)len;
    int fd = open("/dev/rfkill", O_WRONLY | O_CLOEXEC);
    if (fd < 0) { admin_say("ERR", "no /dev/rfkill"); return; }
    struct rfkill_event ev = { .type = RFKILL_TYPE_BLUETOOTH, .op = RFKILL_OP_CHANGE_ALL, .soft = 0 };
    ssize_t w = write(fd, &ev, RFKILL_EVENT_SIZE_V1);
    close(fd);
    if (w != RFKILL_EVENT_SIZE_V1) admin_say("ERR", "rfkill: %s", strerror(errno));
    else admin_say("OK", "unblocked");
}
