/* drmcard.h — qué /dev/dri/cardN es la pantalla (lord-server y lord-sessiond).
 *
 * En una PC suele ser card0, pero no siempre: con simpledrm al arrancar, amdgpu/i915 aparecen
 * como card1. En las placas ARM (Raspberry Pi 4/5 y muchas otras) la pantalla y la GPU 3D son
 * dos dispositivos: en la Pi 5, v3d (solo render, sin conectores) puede quedar en card0 y vc4
 * (la que tiene HDMI) en card1. Por eso se elige la primera tarjeta que tiene conectores y
 * CRTCs, y entre esas, la que tiene un monitor enchufado. LORD_CARD=/dev/dri/cardN la fija.
 *
 * Solo ioctls del kernel (sin libdrm); header sin dependencias para que lo use también
 * lord-sessiond, que no enlaza kms.c. Ver docs/RASPBERRY.md. */
#ifndef LORD_DRMCARD_H
#define LORD_DRMCARD_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* 2 = tiene un conector conectado, 1 = tiene conectores (ninguno conectado), 0 = no es KMS. */
static inline int drm_card_score(int fd) {
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof res);
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) || !res.count_connectors || !res.count_crtcs) return 0;
    uint32_t n = res.count_connectors > 64 ? 64 : res.count_connectors;
    uint32_t ids[64];
    struct drm_mode_card_res r2;
    memset(&r2, 0, sizeof r2);
    r2.connector_id_ptr = (uintptr_t)ids;
    r2.count_connectors = n;
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &r2)) return 1;
    if (r2.count_connectors < n) n = r2.count_connectors;
    for (uint32_t i = 0; i < n; i++) {
        struct drm_mode_get_connector c;
        memset(&c, 0, sizeof c);
        c.connector_id = ids[i];
        if (!ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) && c.connection == 1) return 2;
    }
    return 1;
}

/* Escribe en out la ruta de la tarjeta de la pantalla. 0 si encontró una con KMS; -1 si no
 * (out queda en /dev/dri/card0, como antes). */
static inline int drm_pick_card(char *out, size_t size) {
    const char *env = getenv("LORD_CARD");
    if (env && !strncmp(env, "/dev/dri/card", 13) && !strstr(env, "..")) {
        snprintf(out, size, "%s", env);
        return 0;
    }
    int best = 0, best_i = -1;
    for (int i = 0; i < 16 && best < 2; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/dri/card%d", i);
        int fd = open(p, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        int s = drm_card_score(fd);
        close(fd);
        if (s > best) { best = s; best_i = i; }
    }
    snprintf(out, size, "/dev/dri/card%d", best_i < 0 ? 0 : best_i);
    return best_i < 0 ? -1 : 0;
}

#endif
