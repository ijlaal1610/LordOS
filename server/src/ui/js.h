/* js.h — motor JavaScript (QuickJS) que ejecuta el tema React. */
#ifndef LORD_JS_H
#define LORD_JS_H

#include <stddef.h>

int    js_init(const char *bundle_path);
int    js_init_app(const char *host_bundle, const char *app_id);   /* lord-server --app <id> */
void   js_shutdown(void);
double js_tick(double now);        /* corre timers vencidos; ms hasta el próximo o -1 */
int    js_wants_frame(void);       /* hay requestAnimationFrame pendientes */
void   js_frame(double now);
void   js_pointer(const char *type, int target, float x, float y, int button);
/* Touchpad y escritorio remoto (tp/tp_route.c) → __lordGesture(kind, phase, id, x, y, a, b, c, d):
 * "scroll" (dx, dy, fuente), "pinch" (escala, giro, dx, dy), "swipe" (dx, dy, dedos, del sistema). */
void   js_gesture(const char *kind, const char *phase, int target, float x, float y, double a, double b, double c, double d);
void   js_key(int code, int value);
void   js_wl_event(int id, const char *type, const char *json);
void   js_media_event(int id, const char *type, const char *json);
void   js_term_event(int id, const char *type, const char *data);
void   js_sound_event(const char *type, const char *json);
void   js_net_event(const char *what);
void   js_disk_event(const char *what);      /* Disks app: something changed (disk/disk.c) */
void   js_backup_event(const char *json);    /* Backups → __lordBackup */
void   js_admin_event(int id, const char *kind, const char *text);
void   js_logs_event(const char *kind, int id, const char *name);   /* app Registros */
void   js_clipboard_poll(void);
void   js_remote_event(const char *json);
void   js_greeter_event(const char *json);
void   js_screen_resized(void);
void   js_hw_event(const char *kind, const char *json);   /* Settings' hardware (src/hw) → __lordHw */
void   js_broker_event(const char *kind, const char *json);   /* avisos del broker → tema */
void   js_broker_app_event(const char *kind, const char *json, size_t len);   /* lord-server → app aislada */

#endif
