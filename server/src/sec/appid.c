/* appid.c — app of a process by its cgroup (see appid.h). */
#include "appid.h"
#include "../fw/rules.h"

#include <stdio.h>
#include <string.h>

int appid_of_cgroup(const char *path, char *out, size_t n) {
    const char *p = strstr(path, "/lordos-session-");
    if (!p || !(p = strchr(p + 1, '/'))) return -1;
    p++;
    char scope[FW_APP_MAX + 32];
    size_t l = strcspn(p, "/\n");
    if (l < 18 || l >= sizeof scope || strncmp(p, "app-lordos-", 11) || strncmp(p + l - 6, ".scope", 6)) return -1;
    memcpy(scope, p, l);
    scope[l] = 0;
    return fw_app_canon(scope, out, n);
}

int appid_of_pid(unsigned pid, char *out, size_t n) {
    char f[64], line[1024];
    snprintf(f, sizeof f, "/proc/%u/cgroup", pid);
    FILE *fp = fopen(f, "re");
    if (!fp) return -1;
    int r = -1;
    while (fgets(line, sizeof line, fp))
        if (!strncmp(line, "0::", 3)) { r = appid_of_cgroup(line + 3, out, n) ? 1 : 0; break; }
    fclose(fp);
    return r;
}
