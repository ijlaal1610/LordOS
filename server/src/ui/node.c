/* node.c — árbol de nodos nativos que React crea y modifica. */
#include "ui.h"

#include <stdlib.h>
#include <string.h>

Ui ui;

void style_defaults(Style *s) {
    memset(s, 0, sizeof *s);
    s->align_items = ALIGN_STRETCH;
    s->object_fit = FIT_COVER;
    s->opacity = 1;
    s->color = 0xff000000;
    s->font_size = 14;
    s->font_weight = 400;
    s->grad_angle = 180;
    s->text_align = TEXT_START;
}

Node *node_new(int type) {
    int id = 1;
    while (id < ui.cap && ui.nodes[id]) id++;
    if (id >= ui.cap) {
        int cap = ui.cap ? ui.cap * 2 : 1024;
        Node **n = realloc(ui.nodes, (size_t)cap * sizeof *n);
        if (!n) return NULL;
        memset(n + ui.cap, 0, (size_t)(cap - ui.cap) * sizeof *n);
        ui.nodes = n;
        ui.cap = cap;
    }
    Node *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->id = id;
    n->type = type;
    style_defaults(&n->s);
    ui.nodes[id] = n;
    ui.dirty = 1;
    return n;
}

Node *node_get(int id) { return id > 0 && id < ui.cap ? ui.nodes[id] : NULL; }

int node_count(void) {
    int n = 0;
    for (int i = 0; i < ui.cap; i++) n += ui.nodes[i] != NULL;
    return n;
}

void damage_add(Box b) {
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return;
    Box *d = &ui.damage;
    if (d->x1 <= d->x0 || d->y1 <= d->y0) { *d = b; return; }
    if (b.x0 < d->x0) d->x0 = b.x0;
    if (b.y0 < d->y0) d->y0 = b.y0;
    if (b.x1 > d->x1) d->x1 = b.x1;
    if (b.y1 > d->y1) d->y1 = b.y1;
}

/* Un nodo que sale del árbol deja de verse: su área anterior hay que repintarla. */
void damage_subtree(Node *n) {
    damage_add(n->prev_box);
    n->prev_box = (Box){0, 0, 0, 0};
    for (Node *c = n->first; c; c = c->next) damage_subtree(c);
}

static void invalidate_measure(Node *n) {
    for (; n; n = n->parent) n->measure_valid = 0;
    ui.dirty = 1;
}

void node_invalidate(Node *n) {
    n->paint_dirty = 1;
    if (n->type == NODE_RAW && n->parent) n->parent->paint_dirty = 1;
    invalidate_measure(n);
}

void node_detach(Node *c) {
    Node *p = c->parent;
    if (!p) return;
    damage_subtree(c);
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->prev = c->next = NULL;
    invalidate_measure(p);
}

void node_append(Node *p, Node *c) {
    node_detach(c);
    c->parent = p;
    c->prev = p->last;
    if (p->last) p->last->next = c; else p->first = c;
    p->last = c;
    invalidate_measure(p);
}

void node_insert_before(Node *p, Node *c, Node *before) {
    if (!before || before->parent != p) { node_append(p, c); return; }
    node_detach(c);
    c->parent = p;
    c->next = before;
    c->prev = before->prev;
    if (before->prev) before->prev->next = c; else p->first = c;
    before->prev = c;
    invalidate_measure(p);
}

/* Destruye el nodo y los hijos que sigan colgados de él. */
void node_destroy(Node *n) {
    if (!n) return;
    node_detach(n);
    while (n->first) node_destroy(n->first);
    if (ui.root == n) ui.root = NULL;
    ui.nodes[n->id] = NULL;
    image_release(n->img);
    free(n->text);
    free(n);
    ui.dirty = 1;
}

void node_set_text(Node *n, const char *s) {
    free(n->text);
    n->text = strdup(s ? s : "");
    node_invalidate(n);
}

/* Concatena el texto de los nodos RAW descendientes (<Text>hola {nombre}</Text>). */
static int gather(const Node *n, char *buf, int size, int len) {
    for (const Node *c = n->first; c; c = c->next) {
        if (c->type == NODE_RAW && c->text) {
            int l = (int)strlen(c->text);
            if (len + l >= size) l = size - 1 - len;
            if (l > 0) { memcpy(buf + len, c->text, (size_t)l); len += l; }
        } else if (c->type == NODE_TEXT) {
            len = gather(c, buf, size, len);
        }
    }
    buf[len] = 0;
    return len;
}

int node_gather_text(const Node *n, char *buf, int size) {
    buf[0] = 0;
    return gather(n, buf, size, 0);
}

/* Hijos en orden de pintado: orden del árbol, estable por zIndex. */
int node_children_ordered(Node *n, Node **out, int max) {
    int k = 0, sorted = 1;
    for (Node *c = n->first; c && k < max; c = c->next) {
        out[k++] = c;
        if (c->s.z_index != 0) sorted = 0;
    }
    if (!sorted)
        for (int i = 1; i < k; i++) {
            Node *v = out[i];
            int j = i - 1;
            while (j >= 0 && out[j]->s.z_index > v->s.z_index) { out[j + 1] = out[j]; j--; }
            out[j + 1] = v;
        }
    return k;
}

/* Borra todos los nodos (cambio de tema). La pantalla completa queda para repintar. */
void ui_reset(void) {
    for (int i = 0; i < ui.cap; i++)
        if (ui.nodes[i]) { free(ui.nodes[i]->text); free(ui.nodes[i]); ui.nodes[i] = NULL; }
    ui.root = NULL;
    ui.damage = (Box){0, 0, ui.width, ui.height};
    ui.dirty = 1;
}
