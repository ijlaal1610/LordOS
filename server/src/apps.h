/* apps.h — installed Linux apps (.desktop files) and their launcher (see apps.c). */
#ifndef LORD_APPS_H
#define LORD_APPS_H

#include <limits.h>

typedef struct {
    char id[160];                       /* "org.featherpad.FeatherPad" (.desktop name without the extension) */
    char name[160], generic[160], comment[320], keywords[320], categories[256];
    char wmclass[160];                  /* StartupWMClass: to recognize its Wayland window */
    char mime[1024];                    /* MimeType: file types it opens ("text/plain;…") */
    char icon[PATH_MAX];                /* PNG found, or "" */
    char file[PATH_MAX];                /* the .desktop file */
    char bin[128];                      /* program name from Exec (for the Monitor: process → app) */
    int user;                           /* from ~/.local/share/applications */
} DesktopApp;

/* Lists the apps visible to LordOS (Type=Application, no NoDisplay/Hidden, OnlyShowIn and
 * NotShowIn, TryExec present, not terminal apps). lang = "es-AR": translated names.
 * Returns the count; *out is an internal array valid until the next call. */
int apps_list(const char *lang, const DesktopApp **out);

/* Launches the app with that id: only one from the list above, with its Exec line, without arguments
 * from the caller and without a shell. 0 if the process started. */
int apps_launch(const char *id);
/* Same, but opening a file or folder (see apps.c: only apps with MimeType and paths in the
 * home folder or the user's mounts, as a single argument). */
int apps_launch_with(const char *id, const char *path);
/* Why the last apps_launch/apps_launch_with refused, as an error code ("" = no specific reason). */
const char *apps_last_error(void);

#endif
