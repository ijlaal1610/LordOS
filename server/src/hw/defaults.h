/* defaults.h — default applications on disk (Settings → Default Apps): the user's
 * ~/.config/mimeapps.list and the system ones, shared-mime-info's globs2, and the preferred
 * terminal in ~/.config/xdg-terminals.list (the xdg-terminal-exec convention). LordOS's own apps
 * are written as "lordos-<app>.desktop" (no such file exists; other desktops skip them). */
#ifndef LORD_HW_DEFAULTS_H
#define LORD_HW_DEFAULTS_H

#include <stddef.h>

/* Default app for a MIME type, following the spec's lookup order. 0 if found ("x.desktop"). */
int  defaults_lookup(const char *mime, char *id, size_t n);
/* Sets it in ~/.config/mimeapps.list (atomic write, other lines kept). 0 if written. */
int  defaults_set(const char *mime, const char *id);
/* MIME type of a file name (globs2). 0 if known. */
int  defaults_guess(const char *path, char *mime, size_t n);
/* Linux app to open path with (its desktop id without ".desktop"): the default for its type,
 * if that is a Linux app. 0 if there is one. */
int  defaults_app_for(const char *path, char *id, size_t n);
/* Preferred terminal ("foot.desktop", "lordos-terminal.desktop"). */
int  defaults_terminal(char *id, size_t n);
int  defaults_set_terminal(const char *id);

#endif
