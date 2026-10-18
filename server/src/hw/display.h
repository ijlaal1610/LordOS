/* display.h — Settings → Displays on the lord-server side (docs/SETTINGS.md, "Displays").
 *
 *   - lists the card's outputs with their EDID name, modes and the current one;
 *   - applies a resolution/refresh/output live (the modeset itself runs in main.c, between
 *     frames, through DisplayHost) and saves it per display (EDID identity) in
 *     ~/.config/lordos/displays.json, which the picker uses the next time the monitor is lent;
 *   - Night Shift: a color temperature on the CRTC gamma LUT, or a software LUT applied while
 *     copying the frame when the card has no gamma (virtio-gpu, some ARM boards);
 *   - idle: screen off (DPMS) after N seconds without input, plus "lock" and "sleep" notices for
 *     the theme. Deadlines go into the main loop's poll timeout: no polling;
 *   - hotplug: hw/uevent.c calls display_hotplug() on a DRM "change" uevent.
 *
 * One output at a time: kms.c drives a single CRTC. Mirroring and extending are not
 * implemented (see docs/SETTINGS.md). */
#ifndef LORD_HW_DISPLAY_H
#define LORD_HW_DISPLAY_H

#include <stddef.h>
#include <stdint.h>
#include "../kms.h"
#include "../canvas.h"

typedef struct {
    Kms *(*kms)(void);                  /* the card while the session is on the monitor, else NULL */
    void (*redraw)(void);               /* full damage (the software tint changed, screen back on) */
    void (*event)(const char *kind, const char *json);   /* → __lordHw(kind, data) */
} DisplayHost;

void display_init(const DisplayHost *h);                /* after dropping root (needs $HOME) */
/* For kms_set_picker: the saved mode and main display, by EDID identity. */
int  display_pick(const KmsOutput *o, int n, int *index, struct drm_mode_modeinfo *mode);
void display_attached(void);            /* the session got the monitor (or started on it) */
void display_detached(void);
void display_hotplug(void);             /* DRM uevent: connectors may have changed */

/* JSON for lord.display.list(). Returns a malloc'd string. */
char *display_list_json(void);
/* Apply live (not saved): output id, w×h, refresh in mHz (0 = best), rotation 0..3 (×90°).
 * Queued; main.c runs it between frames (display_take_modeset). 0 if accepted. */
int  display_request(uint32_t conn, int w, int h, int refresh_mhz, int rotation);
/* Save the current configuration of that output as its display's default (and as the main
 * display). 0 if written. */
int  display_save(uint32_t conn);
/* main.c: a queued modeset to run now (no flip pending). 1 if there is one. */
int  display_take_modeset(uint32_t *conn, struct drm_mode_modeinfo *mode);
void display_modeset_done(int ok);      /* main.c after kms_modeset (+ set_size, GPU restart) */
int  display_rotation(void);            /* 0..3: the frame is rotated by software (CPU path) */
/* Logical size for a physical one with the current rotation (90°/270° swap). */
void display_logical_size(int pw, int ph, int *lw, int *lh);

/* Software rotation (CPU path): rect r of the logical frame (lw×lh) in physical coordinates, and
 * the copy of that rect from the logical canvas into the physical one. rot 1 = 90° clockwise. */
Rect display_rotate_rect(Rect r, int lw, int lh, int rot);
void display_rotate_copy(const Canvas *src, Rect r, Canvas *dst, int rot);
/* Same, from a buffer whose first pixel is logical (ox, oy) (a cursor composited apart). */
void display_rotate_copy_at(const uint32_t *px, int stride, int ox, int oy, Rect r, int lw, int lh, Canvas *dst, int rot);
/* Physical normalized position (0..1, as a tablet reports it) → logical normalized position. */
void display_unrotate_point(double px, double py, double *lx, double *ly);

/* Night Shift: kelvin (6500 = off). Hardware gamma when the CRTC has one, else software. */
void display_set_temperature(int kelvin);
int  display_temperature(void);
const uint8_t *display_soft_lut(void);  /* 3×256 (r, g, b) when the software tint is on, else NULL */
void display_tint(Canvas *c, Rect r);   /* apply the software LUT in place */
/* Sunrise/sunset for a date at the time zone's coordinates (zone1970.tab), minutes UTC. */
int  display_sun(const char *zone, int y, int m, int d, double *rise, double *set);

/* Idle (seconds; 0 = never): screen off, lock notice, sleep notice. */
void display_set_idle(int off_s, int lock_s, int sleep_s);
void display_activity(double now_ms);   /* any input */
double display_timeout(double now_ms);  /* ms to the next idle deadline, or -1 */
void display_tick(double now_ms);
int  display_is_off(void);              /* DPMS off: main.c does not render */
void display_wake(void);                /* turn the screen back on now */

#endif
