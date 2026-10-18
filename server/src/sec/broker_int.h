/* broker_int.h — internals shared by the broker's server-side files (brokersrv.c and
 * brokercam.c). Not for the rest of lord-server: use broker.h. */
#ifndef LORD_SEC_BROKER_INT_H
#define LORD_SEC_BROKER_INT_H

#include "broker.h"

/* App id of broker connection `conn` (index), or NULL if it is not in use. */
const char *brokersrv_conn_app(int conn);
/* Event to that app (never blocks). 0 if sent, -1 if the socket is full or gone. */
int  brokersrv_conn_event(int conn, const char *kind, const char *data, size_t len);
/* Reply to request reqid on fd, optionally passing passfd (SCM_RIGHTS). */
int  brokersrv_reply(int fd, uint32_t reqid, int status, const void *data, uint32_t len, int passfd);
/* Camera/microphone indicator for the menu bar (only the session's JS draws it). */
void brokersrv_device(const char *app, const char *kind, int on);
/* System save dialog (powerbox) for `app`; the answer comes back as brokersrv_pick_result(reqid). */
void brokersrv_powerbox_save(uint32_t reqid, const char *app, const char *suggest);

/* ---- brokercam.c: the camera of isolated apps ---- */
void brokercam_handle(int conn, int fd, uint32_t reqid, const char *payload, uint32_t len);
int  brokercam_pick_result(uint32_t reqid, const char *path);   /* 1 if it was a camera save */
void brokercam_conn_closed(int conn);
void brokercam_revoke(const char *app);
void brokercam_tick(void);

#endif
