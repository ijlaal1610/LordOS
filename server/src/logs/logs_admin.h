/* logs_admin.h — la acción logs-system del ayudante (registros del sistema para los
 * administradores; app Registros, ver docs/SISTEMA.md). */
#ifndef LORD_LOGS_ADMIN_H
#define LORD_LOGS_ADMIN_H

#include "../admin.h"

void logs_act_system(const AdminReq *r, const char *arg, size_t len);

/* Solo administradores (sin A_ANYONE); en un proceso aparte porque puede tardar. */
#define LOGS_ADMIN_ACTIONS \
    { "logs-system", logs_act_system, A_FORK },

#endif
