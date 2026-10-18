/* js_media.h — lord.media (ver js_media.c y docs/MEDIOS.md). */
#ifndef LORD_JS_MEDIA_H
#define LORD_JS_MEDIA_H

#include "quickjs.h"

struct Node;
void js_media_register(JSContext *c, JSValue lord);
void js_media_node(struct Node *n, int id);       /* prop `media` de un nodo media */

#endif
