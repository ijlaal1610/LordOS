/* grab.h — mover y cambiar el tamaño de ventanas en C (docs/VENTANAS.md).
 *
 * El tema empieza el arrastre (lord.wm.grab) y lord-server lo sigue solo: cada cuadro corre el
 * nodo de la ventana con el puntero, sin llamar a JS ni volver a pintar su contenido (al mover,
 * la ventana se pinta una vez en una capa y después solo se copia). Al soltar, JS recibe el
 * resultado (__lordWm('end', …)) y actualiza el gestor de ventanas. */
#ifndef LORD_GRAB_H
#define LORD_GRAB_H

#include "ui.h"

typedef struct {
    float inset;            /* el nodo es el marco agrandado `inset` px por lado (bordes para el tamaño) */
    float min_w, min_h;     /* del marco (manifest minSize) */
    float max_w, max_h;     /* 0 = sin máximo */
    float min_top;          /* el marco no sube más que esto (barra de menú) */
} GrabLimits;

/* main.c: dónde está el puntero y qué botones están apretados (máscara, bit = botón del DOM). */
void grab_note_pointer(float x, float y, int buttons);
/* edges: 0 = mover; si no, bordes de xdg (1 arriba, 2 abajo, 4 izquierda, 8 derecha). Solo con un
 * botón apretado. 0 si empezó. */
int  grab_begin(int node, int edges, const GrabLimits *lim);
int  grab_active(void);
int  grab_cursor(void);                     /* forma del cursor durante el arrastre (cursor.h) */
void grab_motion(float x, float y);         /* una vez por cuadro, con el puntero acumulado */
/* Terminó (se soltó el botón): 1 y el cambio del nodo respecto del comienzo. */
int  grab_finish(int *node, int *edges, float *dx, float *dy, float *dw, float *dh);
void grab_cancel(void);

/* paint.c: capa de la ventana que se mueve y lo de abajo. Al cambiar el tamaño (resize = 1) la
 * ventana se repinta en cada cuadro, pero lo de abajo igual se guarda. */
void paint_layer_begin(int node, int resize);
void paint_layer_end(void);
void paint_layer_external_damage(void);     /* algo cambió adentro (terminal, app Linux, video) */
/* Para la medición de arrastres: veces que la capa se pintó y que solo se copió (y vuelve a 0). */
void paint_layer_stats(long *renders, long *blits);

#endif
