/* layout.c — our own flexbox (subset: direction, wrap, grow/shrink, justify, align, gap,
 * margins, padding, %, min/max, position:absolute). With flexWrap: 'wrap' the children that don't
 * fit move to another line (gap also separates the lines; lines go at the start, like
 * align-content: flex-start). Semantics similar to React Native:
 * flexDirection defaults to "column", flexShrink defaults to 0.
 *
 * `direction: 'rtl'` (inherited, as in CSS) flips the main axis of rows and the
 * cross-axis start of columns; left/right, margins and paddings remain
 * physical (as in CSS), so absolute positions —windows, menus— don't change. */
#include "ui.h"

#include <math.h>
#include <stdlib.h>

#define MAX_KIDS 1024

static float res(Dim d, float ref) {
    if (d.unit == UNIT_PX) return d.v;
    if (d.unit == UNIT_PCT && !isnan(ref)) return ref * d.v / 100.0f;
    return NAN;
}

static float clamp_dim(float v, Dim mn, Dim mx, float ref) {
    float a = res(mn, ref), b = res(mx, ref);
    if (!isnan(b) && v > b) v = b;
    if (!isnan(a) && v < a) v = a;
    return v < 0 ? 0 : v;
}

static int in_flow(const Node *c) {
    return c->type != NODE_RAW && !c->s.display_none && !c->s.absolute;
}

static float pad_h(const Style *s) { return s->padding[EDGE_L] + s->padding[EDGE_R] + 2 * s->border_width; }
static float pad_v(const Style *s) { return s->padding[EDGE_T] + s->padding[EDGE_B] + 2 * s->border_width; }

static char text_buf[8192];

/* Intrinsic size (border-box) given the container's available space. */
static void measure(Node *n, float aw, float ah, float *ow, float *oh) {
    const Style *s = &n->s;
    float ph = pad_h(s), pv = pad_v(s);
    float w = res(s->width, aw), h = res(s->height, ah);
    if (isnan(w) || isnan(h)) {
        float cw = 0, ch = 0;
        if (n->type == NODE_TEXT) {
            float maxw = !isnan(w) ? w - ph : !isnan(aw) ? aw - ph : 0;
            float mx = res(s->max_w, aw);
            if (!isnan(mx) && (maxw <= 0 || mx - ph < maxw)) maxw = mx - ph;
            if (maxw < 0) maxw = 0;
            if (!n->measure_valid || fabsf(n->measure_maxw - maxw) > 0.5f) {
                node_gather_text(n, text_buf, sizeof text_buf);
                text_measure(text_buf, TEXT_WEIGHT(s), s->font_size, s->line_height, maxw, s->nowrap,
                             &n->measure_w, &n->measure_h);
                n->measure_maxw = maxw;
                n->measure_valid = 1;
            }
            cw = n->measure_w; ch = n->measure_h;
        } else if (n->type == NODE_IMAGE && n->img) {
            int iw, ih;
            image_size(n->img, &iw, &ih);
            cw = (float)iw; ch = (float)ih;
            if (!isnan(w) && iw) ch = (w - ph) * ih / iw;
            else if (!isnan(h) && ih) cw = (h - pv) * iw / ih;
        } else {
            float iaw = !isnan(w) ? w - ph : !isnan(aw) ? aw - ph : NAN;
            float iah = !isnan(h) ? h - pv : !isnan(ah) ? ah - pv : NAN;
            int count = 0;
            float lim = s->row ? iaw : iah;                 /* with wrap: length of one line */
            int wrap = s->wrap && !isnan(lim);
            float line_m = 0, line_c = 0, tot_m = 0, tot_c = 0;
            int in_line = 0;
            for (Node *c = n->first; c; c = c->next) {
                if (!in_flow(c)) continue;
                const Style *cs = &c->s;
                float mh = cs->margin[EDGE_L] + cs->margin[EDGE_R], mv = cs->margin[EDGE_T] + cs->margin[EDGE_B];
                float c_w, c_h;
                measure(c, isnan(iaw) ? NAN : iaw - mh, isnan(iah) ? NAN : iah - mv, &c_w, &c_h);
                c_w += mh; c_h += mv;
                if (wrap) {
                    float cm = s->row ? c_w : c_h, cc = s->row ? c_h : c_w;
                    if (in_line && line_m + s->gap + cm > lim + 0.5f) {
                        if (line_m > tot_m) tot_m = line_m;
                        tot_c += line_c + s->gap;
                        line_m = line_c = 0;
                        in_line = 0;
                    }
                    line_m += (in_line ? s->gap : 0) + cm;
                    if (cc > line_c) line_c = cc;
                    in_line++;
                    continue;
                }
                if (s->row) { cw += c_w; if (c_h > ch) ch = c_h; }
                else        { ch += c_h; if (c_w > cw) cw = c_w; }
                count++;
            }
            if (wrap) {
                if (line_m > tot_m) tot_m = line_m;
                tot_c += line_c;
                if (s->row) { cw = tot_m; ch = tot_c; } else { ch = tot_m; cw = tot_c; }
            } else if (count > 1) { if (s->row) cw += s->gap * (count - 1); else ch += s->gap * (count - 1); }
        }
        if (isnan(w)) w = cw + ph;
        if (isnan(h)) h = ch + pv;
    }
    *ow = clamp_dim(w, s->min_w, s->max_w, aw);
    *oh = clamp_dim(h, s->min_h, s->max_h, ah);
}

static void layout_node(Node *n, float w, float h);

static void layout_absolute(Node *n, Node *c, float w, float h) {
    const Style *s = &n->s, *cs = &c->s;
    float bw = s->border_width, cbw = w - 2 * bw, cbh = h - 2 * bw;
    float l = res(cs->pos[EDGE_L], cbw), t = res(cs->pos[EDGE_T], cbh);
    float r = res(cs->pos[EDGE_R], cbw), b = res(cs->pos[EDGE_B], cbh);
    float cw = res(cs->width, cbw), ch = res(cs->height, cbh);
    if (isnan(cw) && !isnan(l) && !isnan(r)) cw = cbw - l - r;
    if (isnan(ch) && !isnan(t) && !isnan(b)) ch = cbh - t - b;
    if (isnan(cw) || isnan(ch)) {
        float mw, mh;
        measure(c, isnan(cw) ? cbw : cw, isnan(ch) ? cbh : ch, &mw, &mh);
        if (isnan(cw)) cw = mw;
        if (isnan(ch)) ch = mh;
    }
    cw = clamp_dim(cw, cs->min_w, cs->max_w, cbw);
    ch = clamp_dim(ch, cs->min_h, cs->max_h, cbh);
    float sx = n->rtl ? cbw - s->padding[EDGE_R] - cw - cs->margin[EDGE_R] - cs->margin[EDGE_L] : s->padding[EDGE_L];
    c->x = bw + (!isnan(l) ? l : !isnan(r) ? cbw - r - cw : sx) + cs->margin[EDGE_L];
    c->y = bw + (!isnan(t) ? t : !isnan(b) ? cbh - b - ch : s->padding[EDGE_T]) + cs->margin[EDGE_T];
    layout_node(c, cw, ch);
}

/* Distributes and places a line of children: kids[0..k) on the main axis (main) and in a band
 * of the cross axis [cross_off, cross_off + line_cross) inside the content (cross_total). */
static void layout_line(Node *n, Node **kids, float *size_main, const float *meas_cross, int k, float main,
                        float line_cross, float cross_off, float cross_total, float ix, float iy, float ih) {
    const Style *s = &n->s;
    int row = s->row, rtl = n->rtl;
    float size_cross[MAX_KIDS];
    float used = k > 1 ? s->gap * (k - 1) : 0, grow = 0, shrink = 0;
    for (int i = 0; i < k; i++) {
        const Style *cs = &kids[i]->s;
        used += size_main[i] + (row ? cs->margin[EDGE_L] + cs->margin[EDGE_R] : cs->margin[EDGE_T] + cs->margin[EDGE_B]);
        grow += cs->grow;
        shrink += cs->shrink * size_main[i];
    }

    /* Distribute free space (grow) or missing space (shrink). */
    float free_space = main - used;
    if (free_space > 0 && grow > 0) {
        for (int i = 0; i < k; i++) size_main[i] += free_space * kids[i]->s.grow / grow;
        free_space = 0;
    } else if (free_space < 0 && shrink > 0) {
        for (int i = 0; i < k; i++) {
            size_main[i] += free_space * kids[i]->s.shrink * size_main[i] / shrink;
            if (size_main[i] < 0) size_main[i] = 0;
        }
        free_space = 0;
    }
    if (free_space != 0 || grow > 0) {
        /* min/max on the main axis and recomputation of free space. */
        float total = k > 1 ? s->gap * (k - 1) : 0;
        for (int i = 0; i < k; i++) {
            const Style *cs = &kids[i]->s;
            size_main[i] = clamp_dim(size_main[i], row ? cs->min_w : cs->min_h, row ? cs->max_w : cs->max_h, main);
            total += size_main[i] + (row ? cs->margin[EDGE_L] + cs->margin[EDGE_R] : cs->margin[EDGE_T] + cs->margin[EDGE_B]);
        }
        free_space = main - total;
    }

    for (int i = 0; i < k; i++) {
        Node *c = kids[i];
        const Style *cs = &c->s;
        float mc = row ? cs->margin[EDGE_T] + cs->margin[EDGE_B] : cs->margin[EDGE_L] + cs->margin[EDGE_R];
        int align = cs->align_self != ALIGN_AUTO ? cs->align_self : s->align_items;
        float ec = res(row ? cs->height : cs->width, line_cross);
        float sz;
        if (!isnan(ec)) sz = ec;
        else if (align == ALIGN_STRETCH) sz = line_cross - mc;
        else if (row) {
            /* Child height for the width already decided (wrapping text). */
            float mw, mh;
            measure(c, size_main[i], ih - mc, &mw, &mh);
            sz = mh;
        } else sz = meas_cross[i];
        size_cross[i] = clamp_dim(sz, row ? cs->min_h : cs->min_w, row ? cs->max_h : cs->max_w, line_cross);
    }

    float pos = 0, between = s->gap;
    int fill = free_space > 0 || s->justify == JUSTIFY_CENTER || s->justify == JUSTIFY_END;
    if (fill && k > 0) switch (s->justify) {
        case JUSTIFY_CENTER:  pos = free_space / 2; break;
        case JUSTIFY_END:     pos = free_space; break;
        case JUSTIFY_BETWEEN: if (k > 1) between += free_space / (k - 1); break;
        case JUSTIFY_AROUND:  pos = free_space / k / 2; between += free_space / k; break;
        case JUSTIFY_EVENLY:  pos = free_space / (k + 1); between += free_space / (k + 1); break;
        default: break;
    }

    for (int i = 0; i < k; i++) {
        Node *c = kids[i];
        const Style *cs = &c->s;
        float ml = cs->margin[rtl ? EDGE_R : EDGE_L], mr = cs->margin[rtl ? EDGE_L : EDGE_R];
        float m_start = row ? ml : cs->margin[EDGE_T];
        float m_end   = row ? mr : cs->margin[EDGE_B];
        float c_start = row ? cs->margin[EDGE_T] : ml;
        float c_end   = row ? cs->margin[EDGE_B] : mr;
        int align = cs->align_self != ALIGN_AUTO ? cs->align_self : s->align_items;
        float cpos = c_start;
        if (align == ALIGN_CENTER) cpos = (line_cross - size_cross[i] - c_start - c_end) / 2 + c_start;
        else if (align == ALIGN_END) cpos = line_cross - size_cross[i] - c_end;
        cpos += cross_off;
        pos += m_start;
        if (row) { c->x = rtl ? ix + main - pos - size_main[i] : ix + pos; c->y = iy + cpos; layout_node(c, size_main[i], size_cross[i]); }
        else     { c->x = rtl ? ix + cross_total - cpos - size_cross[i] : ix + cpos; c->y = iy + pos; layout_node(c, size_cross[i], size_main[i]); }
        pos += size_main[i] + m_end + between;
    }
}

static void layout_node(Node *n, float w, float h) {
    n->w = w; n->h = h;
    n->rtl = n->s.dir == DIR_RTL ? 1 : n->s.dir == DIR_LTR ? 0 : n->parent ? n->parent->rtl : ui.rtl;
    if (n->type == NODE_RAW || n->type == NODE_TEXT) return;
    const Style *s = &n->s;
    int row = s->row;
    float ix = s->padding[EDGE_L] + s->border_width, iy = s->padding[EDGE_T] + s->border_width;
    float iw = w - pad_h(s), ih = h - pad_v(s);
    if (iw < 0) iw = 0;
    if (ih < 0) ih = 0;
    float main = row ? iw : ih, cross = row ? ih : iw;

    Node *kids[MAX_KIDS];
    float size_main[MAX_KIDS], meas_cross[MAX_KIDS], outer_cross[MAX_KIDS];
    int k = 0;
    for (Node *c = n->first; c && k < MAX_KIDS; c = c->next) {
        if (!in_flow(c)) continue;
        const Style *cs = &c->s;
        float mc = row ? cs->margin[EDGE_T] + cs->margin[EDGE_B] : cs->margin[EDGE_L] + cs->margin[EDGE_R];
        float basis = res(cs->basis, main);
        float em = res(row ? cs->width : cs->height, main);
        float mw = 0, mh = 0;
        if ((isnan(basis) && isnan(em)) || isnan(res(row ? cs->height : cs->width, cross)) || s->wrap)
            measure(c, row ? iw : iw - mc, row ? ih - mc : ih, &mw, &mh);
        float base = !isnan(basis) ? basis : !isnan(em) ? em : (row ? mw : mh);
        kids[k] = c;
        size_main[k] = base;
        meas_cross[k] = row ? mh : mw;
        float ec = res(row ? cs->height : cs->width, cross);
        outer_cross[k] = (!isnan(ec) ? ec : meas_cross[k]) + mc;
        k++;
    }

    if (!s->wrap) layout_line(n, kids, size_main, meas_cross, k, main, cross, 0, cross, ix, iy, ih);
    else {
        /* lines: break when the next child doesn't fit on the main axis */
        int start = 0;
        float off = 0;
        while (start < k) {
            int end = start;
            float len = 0, lc = 0;
            while (end < k) {
                const Style *cs = &kids[end]->s;
                float mm = row ? cs->margin[EDGE_L] + cs->margin[EDGE_R] : cs->margin[EDGE_T] + cs->margin[EDGE_B];
                float add = (end > start ? s->gap : 0) + size_main[end] + mm;
                if (end > start && len + add > main + 0.5f) break;
                len += add;
                if (outer_cross[end] > lc) lc = outer_cross[end];
                end++;
            }
            layout_line(n, kids + start, size_main + start, meas_cross + start, end - start, main, lc, off, cross, ix, iy, ih);
            off += lc + s->gap;
            start = end;
        }
    }

    for (Node *c = n->first; c; c = c->next)
        if (c->type != NODE_RAW && !c->s.display_none && c->s.absolute) layout_absolute(n, c, w, h);
}

static void place(Node *n, float px, float py) {
    n->ax = px + n->x + n->s.translate_x;
    n->ay = py + n->y + n->s.translate_y;
    for (Node *c = n->first; c; c = c->next) place(c, n->ax, n->ay);
}

void layout_root(Node *root, float w, float h) {
    if (!root) return;
    root->x = root->y = 0;
    layout_node(root, w, h);
    place(root, 0, 0);
}
