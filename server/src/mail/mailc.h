/* mailc.h — lord-server's side of lord-maild (see proto.h and docs/MAIL.md).
 *
 * One lord-maild per session, started on first use with a fixed path (next to this binary), no
 * arguments and a minimal environment. A thread of ours talks to it, so the compositor never
 * waits: results and events queue up and JS reads them with mail_poll() (lord.mail.poll).
 *
 * This side also owns the network policy: lord-maild cannot open sockets, and asks us for a
 * connection to host:port. We connect only to the servers of the configured accounts (read from
 * ~/.config/lordos/mail/accounts.json, which only lord.mail.saveConfig can write) and, for ten
 * minutes, to the image hosts of a message whose remote images the user chose to load. */
#ifndef LORD_MAIL_MAILC_H
#define LORD_MAIL_MAILC_H

#include <stddef.h>
#include <stdint.h>

int   mail_available(void);                 /* 0 if the helper binary is there */
int   mail_start(void);                     /* 0 if running (starts it if needed) */
/* Request: JSON text + fds of files (mail_request takes ownership). id > 0 or -1. */
int   mail_request(const char *json, size_t len, int *fds, int nfds);
char *mail_poll(void);                      /* JSON array of events (malloc) or NULL */
int   mail_pending(void);
/* Re-reads the accounts file, rebuilds the allowed endpoints and tells lord-maild. */
void  mail_reload_config(void);
/* Remote images of one message: allow these http(s) hosts for a while. */
void  mail_allow_hosts(const char *const *urls, int n, int seconds);
const char *mail_config_path(void);         /* ~/.config/lordos/mail/accounts.json */
const char *mail_cache_path(void);          /* ~/.local/share/lordos/mail */

/* Decoded images (ARGB32 premultiplied) returned by lord-maild. */
const uint32_t *mail_image_lock(int handle, int *w, int *h);
void  mail_image_unlock(void);
void  mail_image_release(int handle);

/* Pure check used by the connector and its test: is host:port allowed by this config JSON? */
int   mail_endpoint_allowed(const char *config_json, size_t len, const char *host, int port);

/* Is this resolved address on the public Internet? (Remote-image hosts must be: no loopback,
 * private, link-local, CGNAT, multicast or reserved ranges, also behind v4-mapped/NAT64/6to4.) */
struct sockaddr;
int   mail_addr_public(const struct sockaddr *sa);

#endif
