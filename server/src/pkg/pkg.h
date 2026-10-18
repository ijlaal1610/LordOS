/* pkg.h — paquetes de Debian propios de LordOS (ver pkg.c y docs/PAQUETES.md).
 *
 * Todo lo que modifica el sistema son acciones del ayudante privilegiado (admin.c); lord-server
 * solo lee el índice (idx.h) y la base de dpkg (status.h). */
#ifndef LORD_PKG_H
#define LORD_PKG_H

#include "../admin.h"

void pkg_act_update(const AdminReq *r, const char *arg, size_t len);
void pkg_act_install(const AdminReq *r, const char *arg, size_t len);
void pkg_act_remove(const AdminReq *r, const char *arg, size_t len);
void pkg_act_autoremove(const AdminReq *r, const char *arg, size_t len);
void pkg_act_upgrade(const AdminReq *r, const char *arg, size_t len);
void pkg_act_install_file(const AdminReq *r, const char *arg, size_t len);

/* Acciones del ayudante privilegiado (admin.c) que agrega este módulo. */
#define PKG_ADMIN_ACTIONS \
    { "pkg-update", pkg_act_update, A_FORK }, \
    { "pkg-install", pkg_act_install, A_FORK }, \
    { "pkg-remove", pkg_act_remove, A_FORK }, \
    { "pkg-upgrade", pkg_act_upgrade, A_FORK }, \
    { "pkg-autoremove", pkg_act_autoremove, A_FORK }, \
    { "pkg-install-file", pkg_act_install_file, A_FORK },

#endif
