/* install.h — instalador de LordOS al disco, solo desde el ISO live (ver install.c y docs/ISO.md). */
#ifndef LORD_INSTALL_H
#define LORD_INSTALL_H

#include "../admin.h"

void install_act_disks(const AdminReq *r, const char *arg, size_t len);
void install_act_run(const AdminReq *r, const char *arg, size_t len);
void install_act_reboot(const AdminReq *r, const char *arg, size_t len);

/* Acciones del ayudante privilegiado (admin.c). Todas responden ERR fuera del modo live. */
#define INSTALL_ADMIN_ACTIONS \
    { "install-disks", install_act_disks, 0 }, \
    { "install-run", install_act_run, A_FORK }, \
    { "install-reboot", install_act_reboot, 0 },

#endif
