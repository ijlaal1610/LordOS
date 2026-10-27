/* users_admin.h — the machine's users (Settings → Users), in the administration helper. */
#ifndef LORD_USERS_ADMIN_H
#define LORD_USERS_ADMIN_H

#include "../admin.h"

void users_act_add(const AdminReq *r, const char *arg, size_t len);
void users_act_del(const AdminReq *r, const char *arg, size_t len);
void users_act_passwd(const AdminReq *r, const char *arg, size_t len);
void users_act_admin(const AdminReq *r, const char *arg, size_t len);
void users_act_avatar(const AdminReq *r, const char *arg, size_t len);
void users_act_autologin(const AdminReq *r, const char *arg, size_t len);

/* The user's own password against /etc/shadow, as root (also ssh_root.c: re-authorization). 1 if it matches. */
int users_check_password(const char *user, const char *pass);

/* user-passwd and user-avatar: everyone can change their own (validated inside). */
#define USERS_ADMIN_ACTIONS \
    { "user-add", users_act_add, A_FORK }, \
    { "user-del", users_act_del, A_FORK }, \
    { "user-passwd", users_act_passwd, A_FORK | A_ANYONE }, \
    { "user-admin", users_act_admin, 0 }, \
    { "user-avatar", users_act_avatar, A_FORK | A_ANYONE }, \
    { "user-autologin", users_act_autologin, 0 },

#endif
