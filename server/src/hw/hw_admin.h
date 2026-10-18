/* hw_admin.h — the admin helper's actions for Settings (displays, battery, date and time,
 * Bluetooth). Every action is closed: validated arguments, fixed program paths and config files
 * generated here, never user text passed to a shell. Listed in AGENTS.md and docs/SETTINGS.md.
 *
 *   display-brightness <name> <value>  /sys/class/backlight/<name>/brightness (0..max)     anyone
 *   power-profile <p>                  performance|balanced|power-saver: powerprofilesctl if
 *                                      installed, else the cpufreq governor (and EPP)         anyone
 *   power-suspend                      systemctl suspend                                      anyone
 *   power-buttons <power> <lid> <lidac> /etc/systemd/logind.conf.d/lordos.conf + reload logind admin
 *   time-ntp on|off                    timedatectl set-ntp                                    admin
 *   time-set YYYY-MM-DD HH:MM:SS       timedatectl set-time (automatic time must be off)       admin
 *   bt-unblock                         rfkill: unblock Bluetooth (/dev/rfkill)               anyone
 */
#ifndef LORD_HW_ADMIN_H
#define LORD_HW_ADMIN_H

#include "../admin.h"

void hw_act_brightness(const AdminReq *r, const char *arg, size_t len);
void hw_act_power_profile(const AdminReq *r, const char *arg, size_t len);
void hw_act_suspend(const AdminReq *r, const char *arg, size_t len);
void hw_act_buttons(const AdminReq *r, const char *arg, size_t len);
void hw_act_ntp(const AdminReq *r, const char *arg, size_t len);
void hw_act_time_set(const AdminReq *r, const char *arg, size_t len);
void hw_act_bt_unblock(const AdminReq *r, const char *arg, size_t len);

/* Validators, exposed for tests. */
int hw_button_action_valid(const char *a, int lid);
int hw_time_valid(const char *t);       /* "YYYY-MM-DD HH:MM:SS", a real date from 2000 to 2099 */
int hw_backlight_name_valid(const char *n);

#define HW_ADMIN_ACTIONS \
    { "display-brightness", hw_act_brightness, A_ANYONE }, \
    { "power-profile", hw_act_power_profile, A_ANYONE }, \
    { "power-suspend", hw_act_suspend, A_ANYONE }, \
    { "power-buttons", hw_act_buttons, 0 }, \
    { "time-ntp", hw_act_ntp, 0 }, \
    { "time-set", hw_act_time_set, 0 }, \
    { "bt-unblock", hw_act_bt_unblock, A_ANYONE },

#endif
