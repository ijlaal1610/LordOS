/* display.c — Settings → Displays, lord-server side (see display.h). */
#include "display.h"
#include "edid.h"
#include "jbuf.h"
#include "modes.h"
#include "night.h"
#include "../sec/json.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_SAVED 16
#define ID_MAX 96

typedef struct { char id[ID_MAX]; int w, h, refresh, rot; } Saved;

static struct {
    const DisplayHost *host;
    char path[PATH_MAX];                /* ~/.config/lordos/displays.json */
    Saved saved[MAX_SAVED];
    int nsaved;
    char main_id[ID_MAX];
    /* Night Shift */
    int kelvin, soft;
    uint8_t lut[3][256];
    /* idle */
    int off_s, lock_s, sleep_s;
    double last;
    int done_off, done_lock, done_sleep, off;
    /* queued modeset */
    int pending, prot, rot, prev_rot;
    uint32_t pconn;
    struct drm_mode_modeinfo pmode;
} D = { .kelvin = NIGHT_K_MAX };

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static Kms *kms(void) { return D.host && D.host->kms ? D.host->kms() : NULL; }
static void event(const char *kind, const char *json) { if (D.host && D.host->event) D.host->event(kind, json); }

/* ---- identity ---- */

static void output_identity(const KmsOutput *o, char *id, size_t n, char *name, size_t nn) {
    Edid e;
    if (o->edid && edid_parse(o->edid, o->edid_len, &e) == 0) {
        edid_identity(&e, id, n);
        if (name) edid_display_name(&e, name, nn);
        return;
    }
    snprintf(id, n, "%s-%u", kms_connector_type_name(o->type), o->type_id);
    if (name) snprintf(name, nn, "%s", id);
}

static Saved *saved_for(const char *id) {
    for (int i = 0; i < D.nsaved; i++) if (!strcmp(D.saved[i].id, id)) return &D.saved[i];
    return NULL;
}

/* ---- displays.json ---- */

static void load(void) {
    D.nsaved = 0;
    D.main_id[0] = 0;
    int fd = open(D.path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    char buf[16384];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0) return;
    JDoc doc;
    if (json_parse(buf, (size_t)r, &doc, NULL, 0)) { fprintf(stderr, "displays: invalid displays.json\n"); return; }
    const char *m = json_str(doc.root, "main");
    if (m && strlen(m) < ID_MAX) snprintf(D.main_id, sizeof D.main_id, "%s", m);
    const JVal *ds = json_get(doc.root, "displays");
    for (const JVal *v = ds && ds->type == J_OBJ ? ds->kid : NULL; v && D.nsaved < MAX_SAVED; v = v->next) {
        if (v->type != J_OBJ || !v->key || strlen(v->key) >= ID_MAX) continue;
        Saved *s = &D.saved[D.nsaved++];
        snprintf(s->id, sizeof s->id, "%s", v->key);
        s->w = (int)json_num(v, "w", 0);
        s->h = (int)json_num(v, "h", 0);
        s->refresh = (int)json_num(v, "refresh", 0);
        s->rot = (int)json_num(v, "rotation", 0) & 3;
        if (s->w < 0 || s->w > 16384 || s->h < 0 || s->h > 16384) s->w = s->h = 0;
    }
    json_free(&doc);
}

static int store(void) {
    if (!D.path[0]) return -1;
    HBuf b = { 0 };
    hb_printf(&b, "{\n  \"main\": ");
    hb_str(&b, D.main_id);
    hb_printf(&b, ",\n  \"displays\": {");
    for (int i = 0; i < D.nsaved; i++) {
        const Saved *s = &D.saved[i];
        hb_printf(&b, "%s\n    ", i ? "," : "");
        hb_str(&b, s->id);
        hb_printf(&b, ": { \"w\": %d, \"h\": %d, \"refresh\": %d, \"rotation\": %d }", s->w, s->h, s->refresh, s->rot);
    }
    hb_printf(&b, "\n  }\n}\n");
    char *txt = hb_take(&b);
    if (!txt) return -1;
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", D.path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    int ok = fd >= 0 && write(fd, txt, strlen(txt)) == (ssize_t)strlen(txt);
    if (fd >= 0) { ok = fsync(fd) == 0 && ok; close(fd); }
    free(txt);
    if (!ok || rename(tmp, D.path)) { unlink(tmp); return -1; }
    return 0;
}

void display_init(const DisplayHost *h) {
    D.host = h;
    const char *home = getenv("HOME");
    if (home && home[0] == '/') {
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s/.config", home);
        mkdir(dir, 0700);
        snprintf(dir, sizeof dir, "%s/.config/lordos", home);
        mkdir(dir, 0700);
        snprintf(D.path, sizeof D.path, "%s/.config/lordos/displays.json", home);
        load();
    }
    /* lord-server started on its own card as root (development): the picker ran before $HOME was
     * the user's, so the saved mode is applied now */
    Kms *k = kms();
    if (!k) return;
    KmsOutput outs[KMS_MAX_OUTPUTS];
    int n = kms_outputs(k->fd, outs, KMS_MAX_OUTPUTS), idx = -1;
    struct drm_mode_modeinfo m;
    if (display_pick(outs, n, &idx, &m) == 0 && idx >= 0 &&
        (outs[idx].id != k->conn_id || m.hdisplay != k->mode.hdisplay || m.vdisplay != k->mode.vdisplay || mode_refresh_mhz(&m) != mode_refresh_mhz(&k->mode) || D.prot != D.rot)) {
        D.pending = 1;
        D.pconn = outs[idx].id;
        D.pmode = m;
    }
    kms_outputs_free(outs, n);
}

int display_pick(const KmsOutput *o, int n, int *index, struct drm_mode_modeinfo *mode) {
    int sel = -1;
    char id[ID_MAX];
    for (int i = 0; i < n && sel < 0; i++) {
        if (!o[i].connected || !o[i].nmodes) continue;
        output_identity(&o[i], id, sizeof id, NULL, 0);
        if (D.main_id[0] && !strcmp(id, D.main_id)) sel = i;
    }
    for (int i = 0; i < n && sel < 0; i++) if (o[i].connected && o[i].nmodes) sel = i;
    if (sel < 0) return -1;
    output_identity(&o[sel], id, sizeof id, NULL, 0);
    const Saved *s = saved_for(id);
    int m = s && s->w ? mode_pick(o[sel].modes, o[sel].nmodes, s->w, s->h, s->refresh) : -1;
    if (m < 0) m = mode_preferred(o[sel].modes, o[sel].nmodes);
    if (m < 0) return -1;
    *index = sel;
    *mode = o[sel].modes[m];
    D.prot = s ? s->rot : 0;
    D.rot = D.prot;                     /* kms_open/kms_attach use this mode right away */
    return 0;
}

/* ---- Night Shift ---- */

static void apply_tint(void) {
    Kms *k = kms();
    double r, g, b;
    night_temp_rgb(D.kelvin, &r, &g, &b);
    int was_soft = D.soft;
    D.soft = 0;
    int gs = k && !getenv("LORD_SOFT_GAMMA") ? kms_gamma_size(k) : 0;
    if (gs > 1 && gs <= 4096) {
        uint16_t *ramp = malloc((size_t)gs * 3 * sizeof *ramp);
        if (ramp) {
            night_ramp(gs, r, g, b, 1, ramp, ramp + gs, ramp + 2 * gs);
            if (kms_set_gamma(k, gs, ramp, ramp + gs, ramp + 2 * gs)) gs = 0;
            free(ramp);
        } else gs = 0;
    } else gs = 0;
    if (!gs && D.kelvin != NIGHT_K_MAX) {
        uint16_t R[256], G[256], B[256];
        night_ramp(256, r, g, b, 1, R, G, B);
        for (int i = 0; i < 256; i++) {
            D.lut[0][i] = (uint8_t)(R[i] >> 8);
            D.lut[1][i] = (uint8_t)(G[i] >> 8);
            D.lut[2][i] = (uint8_t)(B[i] >> 8);
        }
        D.soft = 1;
    }
    if ((D.soft || was_soft) && D.host && D.host->redraw) D.host->redraw();
    if (D.soft != was_soft) {                   /* Settings shows when the tint is by software */
        char *j = display_list_json();
        if (j) { event("display", j); free(j); }
    }
}

void display_set_temperature(int kelvin) {
    if (kelvin < NIGHT_K_MIN) kelvin = NIGHT_K_MIN;
    if (kelvin > NIGHT_K_MAX) kelvin = NIGHT_K_MAX;
    if (kelvin == D.kelvin) return;
    D.kelvin = kelvin;
    apply_tint();
}

int display_temperature(void) { return D.kelvin; }
const uint8_t *display_soft_lut(void) { return D.soft ? &D.lut[0][0] : NULL; }

void display_tint(Canvas *c, Rect r) {
    if (!D.soft) return;
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > c->w) r.x1 = c->w;
    if (r.y1 > c->h) r.y1 = c->h;
    const uint8_t *lr = D.lut[0], *lg = D.lut[1], *lb = D.lut[2];
    for (int y = r.y0; y < r.y1; y++) {
        uint32_t *p = c->px + (size_t)y * c->stride;
        for (int x = r.x0; x < r.x1; x++) {
            uint32_t v = p[x];
            p[x] = (v & 0xff000000u) | (uint32_t)lr[v >> 16 & 255] << 16 | (uint32_t)lg[v >> 8 & 255] << 8 | lb[v & 255];
        }
    }
}

int display_sun(const char *zone, int y, int m, int d, double *rise, double *set) {
    static char *tab;
    if (!tab) {
        const char *paths[] = { "/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab" };
        for (int i = 0; i < 2 && !tab; i++) {
            FILE *f = fopen(paths[i], "re");
            if (!f) continue;
            tab = calloc(1, 65536);
            if (tab && fread(tab, 1, 65535, f) == 0) { free(tab); tab = NULL; }
            fclose(f);
        }
        if (!tab) return -1;
    }
    double lat, lon;
    if (!zone || night_zone_coords(tab, zone, &lat, &lon)) return -1;
    return night_sun_times(y, m, d, lat, lon, rise, set);
}

/* ---- outputs, modes ---- */

static void out_json(HBuf *b, const KmsOutput *o, const Kms *k) {
    char id[ID_MAX], name[64], conn[48];
    output_identity(o, id, sizeof id, name, sizeof name);
    snprintf(conn, sizeof conn, "%s-%u", kms_connector_type_name(o->type), o->type_id);
    Edid e;
    int has_edid = o->edid && edid_parse(o->edid, o->edid_len, &e) == 0;
    int current = k && o->id == k->conn_id;
    hb_printf(b, "{\"id\":%u,\"connector\":", o->id);
    hb_str(b, conn);
    hb_printf(b, ",\"identity\":");
    hb_str(b, id);
    hb_printf(b, ",\"name\":");
    hb_str(b, name);
    hb_printf(b, ",\"builtin\":%s,\"connected\":%s,\"current\":%s,\"mmWidth\":%u,\"mmHeight\":%u,\"diagonal\":%d,\"vendor\":",
              o->type == DRM_MODE_CONNECTOR_eDP || o->type == DRM_MODE_CONNECTOR_LVDS || o->type == DRM_MODE_CONNECTOR_DSI ? "true" : "false",
              o->connected ? "true" : "false", current ? "true" : "false", o->mm_w, o->mm_h, has_edid ? edid_diagonal_tenths(&e) : 0);
    hb_str(b, has_edid ? e.vendor : "");
    int pref = mode_preferred(o->modes, o->nmodes);
    hb_printf(b, ",\"preferred\":{\"w\":%d,\"h\":%d}", pref >= 0 ? o->modes[pref].hdisplay : 0, pref >= 0 ? o->modes[pref].vdisplay : 0);
    if (current) hb_printf(b, ",\"mode\":{\"w\":%u,\"h\":%u,\"refresh\":%d}", k->mode.hdisplay, k->mode.vdisplay, mode_refresh_mhz(&k->mode));
    const Saved *s = saved_for(id);
    if (s) hb_printf(b, ",\"saved\":{\"w\":%d,\"h\":%d,\"refresh\":%d,\"rotation\":%d}", s->w, s->h, s->refresh, s->rot);
    hb_printf(b, ",\"resolutions\":[");
    int res[64];
    int nr = mode_resolutions(o->modes, o->nmodes, res, 64);
    for (int i = 0; i < nr; i++) {
        int w = o->modes[res[i]].hdisplay, h = o->modes[res[i]].vdisplay, rates[16];
        int nrt = mode_rates(o->modes, o->nmodes, w, h, rates, 16);
        hb_printf(b, "%s{\"w\":%d,\"h\":%d,\"rates\":[", i ? "," : "", w, h);
        for (int j = 0; j < nrt; j++) hb_printf(b, "%s%d", j ? "," : "", rates[j]);
        hb_printf(b, "]}");
    }
    hb_printf(b, "]}");
}

char *display_list_json(void) {
    Kms *k = kms();
    HBuf b = { 0 };
    hb_printf(&b, "{\"onMonitor\":%s,\"rotation\":%d,\"temperature\":%d,\"gammaSize\":%d,\"softTint\":%s,\"maxOutputs\":1,\"outputs\":[",
              k ? "true" : "false", D.rot, D.kelvin, k ? kms_gamma_size(k) : 0, D.soft ? "true" : "false");
    if (k) {
        KmsOutput outs[KMS_MAX_OUTPUTS];
        int n = kms_outputs(k->fd, outs, KMS_MAX_OUTPUTS), first = 1;
        for (int i = 0; i < n; i++) {
            if (!outs[i].connected) continue;
            if (!first) hb_printf(&b, ",");
            first = 0;
            out_json(&b, &outs[i], k);
        }
        kms_outputs_free(outs, n);
    }
    hb_printf(&b, "],\"backlight\":[");
    /* backlight: readable by anyone; writing goes through the display-brightness admin action */
    DIR *d = opendir("/sys/class/backlight");
    int first = 1;
    for (struct dirent *e; d && (e = readdir(d));) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 64) continue;
        char p[160], v[32] = "", mx[32] = "", ty[32] = "";
        const char *names[3] = { "brightness", "max_brightness", "type" };
        char *outs[3] = { v, mx, ty };
        for (int i = 0; i < 3; i++) {
            snprintf(p, sizeof p, "/sys/class/backlight/%s/%s", e->d_name, names[i]);
            int fd = open(p, O_RDONLY | O_CLOEXEC);
            if (fd < 0) continue;
            ssize_t r = read(fd, outs[i], 31);
            close(fd);
            if (r > 0) { outs[i][r] = 0; outs[i][strcspn(outs[i], "\n")] = 0; }
        }
        hb_printf(&b, "%s{\"name\":", first ? "" : ",");
        hb_str(&b, e->d_name);
        hb_printf(&b, ",\"value\":%ld,\"max\":%ld,\"type\":", atol(v), atol(mx));
        hb_str(&b, ty);
        hb_printf(&b, "}");
        first = 0;
    }
    if (d) closedir(d);
    hb_printf(&b, "]}");
    return hb_take(&b);
}

int display_request(uint32_t conn, int w, int h, int refresh_mhz, int rotation) {
    Kms *k = kms();
    if (!k || rotation < 0 || rotation > 3) return -1;
    KmsOutput outs[KMS_MAX_OUTPUTS];
    int n = kms_outputs(k->fd, outs, KMS_MAX_OUTPUTS), ret = -1;
    for (int i = 0; i < n; i++) {
        if (outs[i].id != conn || !outs[i].connected) continue;
        int m = mode_pick(outs[i].modes, outs[i].nmodes, w, h, refresh_mhz);
        if (m < 0) break;
        D.pending = 1;
        D.pconn = conn;
        D.pmode = outs[i].modes[m];
        D.prot = rotation;
        ret = 0;
    }
    kms_outputs_free(outs, n);
    return ret;
}

int display_take_modeset(uint32_t *conn, struct drm_mode_modeinfo *mode) {
    if (!D.pending || !kms()) return 0;
    D.pending = 0;
    *conn = D.pconn;
    *mode = D.pmode;
    D.prev_rot = D.rot;
    D.rot = D.prot;
    return 1;
}

void display_modeset_done(int ok) {
    if (!ok) D.rot = D.prev_rot;
    apply_tint();                       /* a new CRTC (or a modeset) can reset its LUT */
    char *j = display_list_json();
    if (j) { event("display", j); free(j); }
}

int display_rotation(void) { return D.rot; }

void display_logical_size(int pw, int ph, int *lw, int *lh) {
    int swap = D.rot & 1;
    *lw = swap ? ph : pw;
    *lh = swap ? pw : ph;
}

int display_save(uint32_t conn) {
    Kms *k = kms();
    if (!k || conn != k->conn_id) return -1;
    KmsOutput outs[KMS_MAX_OUTPUTS];
    int n = kms_outputs(k->fd, outs, KMS_MAX_OUTPUTS), ret = -1;
    for (int i = 0; i < n; i++) {
        if (outs[i].id != conn) continue;
        char id[ID_MAX];
        output_identity(&outs[i], id, sizeof id, NULL, 0);
        Saved *s = saved_for(id);
        if (!s && D.nsaved < MAX_SAVED) s = &D.saved[D.nsaved++];
        if (!s) s = &D.saved[MAX_SAVED - 1];
        snprintf(s->id, sizeof s->id, "%s", id);
        s->w = k->mode.hdisplay;
        s->h = k->mode.vdisplay;
        s->refresh = mode_refresh_mhz(&k->mode);
        s->rot = D.rot;
        snprintf(D.main_id, sizeof D.main_id, "%s", id);
        ret = store();
    }
    kms_outputs_free(outs, n);
    return ret;
}

void display_attached(void) {
    D.off = 0;
    D.last = now_ms();
    D.done_off = D.done_lock = D.done_sleep = 0;
    apply_tint();
    char *j = display_list_json();
    if (j) { event("display", j); free(j); }
}

void display_detached(void) { D.off = 0; D.soft = D.soft && D.kelvin != NIGHT_K_MAX; }

void display_hotplug(void) {
    Kms *k = kms();
    if (k) {
        KmsOutput outs[KMS_MAX_OUTPUTS];
        int n = kms_outputs(k->fd, outs, KMS_MAX_OUTPUTS), still = 0;
        for (int i = 0; i < n; i++) if (outs[i].id == k->conn_id && outs[i].connected) still = 1;
        int idx = -1;
        struct drm_mode_modeinfo m;
        /* the display in use was unplugged: move to another one (as the picker would) */
        if (!still && display_pick(outs, n, &idx, &m) == 0 && idx >= 0) {
            D.pending = 1;
            D.pconn = outs[idx].id;
            D.pmode = m;
            fprintf(stderr, "displays: the display in use was unplugged; moving to %u\n", outs[idx].id);
        }
        kms_outputs_free(outs, n);
    }
    char *j = display_list_json();
    if (j) { event("display", j); free(j); }
}

/* ---- idle ---- */

void display_set_idle(int off_s, int lock_s, int sleep_s) {
    D.off_s = off_s > 0 ? off_s : 0;
    D.lock_s = lock_s > 0 ? lock_s : 0;
    D.sleep_s = sleep_s > 0 ? sleep_s : 0;
    D.last = now_ms();
    D.done_off = D.done_lock = D.done_sleep = 0;
}

void display_wake(void) {
    if (!D.off) return;
    Kms *k = kms();
    D.off = 0;
    if (k) kms_dpms(k, 1);
    if (D.host && D.host->redraw) D.host->redraw();
    event("idle", "{\"stage\":\"wake\"}");
}

void display_activity(double now) {
    D.last = now;
    D.done_off = D.done_lock = D.done_sleep = 0;
    if (D.off) display_wake();
}

static double deadline(int secs, int done) { return secs && !done ? D.last + secs * 1000.0 : -1; }

double display_timeout(double now) {
    if (!kms()) return -1;
    double best = -1, ds[3] = { deadline(D.off_s, D.done_off), deadline(D.lock_s, D.done_lock), deadline(D.sleep_s, D.done_sleep) };
    for (int i = 0; i < 3; i++) {
        if (ds[i] < 0) continue;
        double left = ds[i] - now;
        if (left < 0) left = 0;
        if (best < 0 || left < best) best = left;
    }
    return best;
}

void display_tick(double now) {
    Kms *k = kms();
    if (!k) return;
    double off = deadline(D.off_s, D.done_off), lock = deadline(D.lock_s, D.done_lock), slp = deadline(D.sleep_s, D.done_sleep);
    if (off >= 0 && now >= off) {
        D.done_off = 1;
        if (kms_dpms(k, 0) == 0) D.off = 1;
        else fprintf(stderr, "displays: the card did not take DPMS off\n");
        event("idle", "{\"stage\":\"off\"}");
    }
    if (lock >= 0 && now >= lock) { D.done_lock = 1; event("idle", "{\"stage\":\"lock\"}"); }
    if (slp >= 0 && now >= slp) { D.done_sleep = 1; event("idle", "{\"stage\":\"sleep\"}"); }
}

int display_is_off(void) { return D.off; }

/* ---- software rotation ---- */

Rect display_rotate_rect(Rect r, int lw, int lh, int rot) {
    switch (rot & 3) {
    case 1: return (Rect){ lh - r.y1, r.x0, lh - r.y0, r.x1 };       /* (x, y) → (lh-1-y, x) */
    case 2: return (Rect){ lw - r.x1, lh - r.y1, lw - r.x0, lh - r.y0 };
    case 3: return (Rect){ r.y0, lw - r.x1, r.y1, lw - r.x0 };        /* (x, y) → (y, lw-1-x) */
    default: return r;
    }
}

/* Copies rect r (logical coordinates, inside lw×lh) to dst rotated; the logical pixel (x, y) is
 * px[(y - oy) * stride + (x - ox)]. */
void display_rotate_copy_at(const uint32_t *px, int stride, int ox, int oy, Rect r, int lw, int lh, Canvas *dst, int rot) {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > lw) r.x1 = lw;
    if (r.y1 > lh) r.y1 = lh;
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return;
    rot &= 3;
    for (int y = r.y0; y < r.y1; y++) {
        const uint32_t *s = px + (size_t)(y - oy) * stride;
        switch (rot) {
        case 0: memcpy(dst->px + (size_t)y * dst->stride + r.x0, s + (r.x0 - ox), (size_t)(r.x1 - r.x0) * 4); break;
        case 1: { uint32_t *col = dst->px + (lh - 1 - y);                     /* column lh-1-y, rows x */
                  for (int x = r.x0; x < r.x1; x++) col[(size_t)x * dst->stride] = s[x - ox];
                  break; }
        case 2: { uint32_t *row = dst->px + (size_t)(lh - 1 - y) * dst->stride;
                  for (int x = r.x0; x < r.x1; x++) row[lw - 1 - x] = s[x - ox];
                  break; }
        case 3: { uint32_t *col = dst->px + y;                                /* column y, rows lw-1-x */
                  for (int x = r.x0; x < r.x1; x++) col[(size_t)(lw - 1 - x) * dst->stride] = s[x - ox];
                  break; }
        }
    }
}

void display_rotate_copy(const Canvas *src, Rect r, Canvas *dst, int rot) {
    display_rotate_copy_at(src->px, src->stride, 0, 0, r, src->w, src->h, dst, rot);
}

void display_unrotate_point(double px, double py, double *lx, double *ly) {
    switch (D.rot & 3) {
    case 1: *lx = py; *ly = 1 - px; break;
    case 2: *lx = 1 - px; *ly = 1 - py; break;
    case 3: *lx = 1 - py; *ly = px; break;
    default: *lx = px; *ly = py; break;
    }
}
