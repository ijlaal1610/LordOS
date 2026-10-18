/* portal.h — org.freedesktop.portal.Settings propio en el bus de la sesión (ver portal.c). */
#ifndef LORD_PORTAL_H
#define LORD_PORTAL_H

int  portal_start(void);                /* después de soltar privilegios; 0 si atiende el nombre */
int  portal_fd(void);                   /* para poll; -1 si no está */
int  portal_wants_write(void);          /* hay mensajes sin mandar (POLLOUT) */
void portal_dispatch(void);
/* Desde JS (lord.portal.set): modo, acento (0..1), nombre de acento de GNOME y orden de botones de GTK. */
void portal_set(int dark, const double rgb[3], const char *accent_name, const char *button_layout);

#endif
