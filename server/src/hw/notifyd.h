/* notifyd.h — org.freedesktop.Notifications on the session bus, so notifications from Linux apps
 * (GTK, Qt, Electron, notify-send) appear as LordOS banners and follow Settings → Notifications
 * (per-app style, sound, Do Not Disturb). The banner is drawn by the theme (sdk/notify.js); this
 * only translates the protocol (spec 1.2): Notify, CloseNotification, GetCapabilities,
 * GetServerInformation, and the NotificationClosed / ActionInvoked signals. Body markup is reduced
 * to plain text. Uses our own D-Bus client (vault/dbus.c).
 *
 * The firewall's "no notifications" (docs/FIREWALL.md, "Linux apps"): before showing a Notify,
 * the bus is asked for the caller's PID (GetConnectionUnixProcessID, answered by the bus from the
 * socket's credentials, not by the app), the PID's cgroup gives its app scope and so its firewall
 * id; if that app is restricted the notification is dropped and the call still gets a valid id,
 * so the app carries on as if it had been shown. Applies to the next notification. */
#ifndef LORD_HW_NOTIFYD_H
#define LORD_HW_NOTIFYD_H

#include <stddef.h>
#include <stdint.h>

typedef void (*NotifydEvent)(const char *kind, const char *json);   /* "notify", "notify-close" */

int  notifyd_start(NotifydEvent fn);    /* 0 if we own the name */
int  notifyd_fd(void);
int  notifyd_wants_write(void);
void notifyd_dispatch(void);
/* From the theme: the banner went away (reason: 1 expired, 2 dismissed, 3 closed by a call) or
 * an action was clicked. */
void notifyd_closed(uint32_t id, uint32_t reason);
void notifyd_action(uint32_t id, const char *key);
/* Firewall id of the app whose cgroup path (the "0::" line of /proc/<pid>/cgroup, without the
 * "0::") is given: a launcher scope ".../lordos-session-<uid>/app-lordos-<id>.scope[/...]" gives
 * "desktop:<id>" (or "lpk:<id>"). 0 ok, -1 not in an app's scope. */
int notifyd_app_of_cgroup(const char *path, char *out, size_t n);
/* Strips markup (<b>, <i>, <a href>, <img>...) and decodes &amp; &lt; &gt; &quot; &apos;. */
void notifyd_plain(const char *in, char *out, size_t n);

#endif
