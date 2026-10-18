/* notifyd.h — org.freedesktop.Notifications on the session bus, so notifications from Linux apps
 * (GTK, Qt, Electron, notify-send) appear as LordOS banners and follow Settings → Notifications
 * (per-app style, sound, Do Not Disturb). The banner is drawn by the theme (sdk/notify.js); this
 * only translates the protocol (spec 1.2): Notify, CloseNotification, GetCapabilities,
 * GetServerInformation, and the NotificationClosed / ActionInvoked signals. Body markup is reduced
 * to plain text. Uses our own D-Bus client (vault/dbus.c). */
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
/* Strips markup (<b>, <i>, <a href>, <img>...) and decodes &amp; &lt; &gt; &quot; &apos;. */
void notifyd_plain(const char *in, char *out, size_t n);

#endif
