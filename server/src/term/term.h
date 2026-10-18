/* term.h — motor de terminal de LordOS: emulador xterm (vt.c), PTY (pty.c),
 * sesiones y eventos (term.c) y dibujo de la grilla (term_paint.c).
 *
 * La emulación y el dibujo están en C para que `cat` de archivos grandes o `htop` no
 * pasen por JavaScript; la app Terminal (React) solo maneja pestañas, paneles, perfiles
 * y atajos, y habla con este módulo a través de lord.term.* (ui/js_term.c). */
#ifndef LORD_TERM_H
#define LORD_TERM_H

#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

#include "../canvas.h"

#define TERM_MAX 64
#define TERM_MAX_COLS 1000
#define TERM_MAX_ROWS 500
#define REPLAY_FRAMES 1200      /* cuadros de Instant Replay por sesión */

/* Atributos de celda. */
enum {
    A_BOLD = 1 << 0, A_DIM = 1 << 1, A_ITALIC = 1 << 2, A_UNDER = 1 << 3, A_UNDER2 = 1 << 4,
    A_CURLY = 1 << 5, A_BLINK = 1 << 6, A_INVERSE = 1 << 7, A_HIDDEN = 1 << 8, A_STRIKE = 1 << 9,
    A_WIDE = 1 << 10, A_WIDE_TAIL = 1 << 11, A_OVERLINE = 1 << 12,
};

/* Colores de celda: 0 = el del perfil; 0x01000000|índice = paleta; 0x02RRGGBB = color directo. */
#define COLOR_DEFAULT 0u
#define COLOR_IDX(i) (0x01000000u | (uint32_t)(i))
#define COLOR_RGB(c) (0x02000000u | ((uint32_t)(c) & 0xffffffu))

typedef struct { uint32_t cp, fg, bg; uint16_t attr, link; } Cell;

/* Grafemas de más de un carácter (e + acento combinante, 👍🏽, 👨‍👩‍👧, banderas): cp =
 * CP_CLUSTER | índice en una tabla global interna (vt.c), compartida por todas las sesiones. */
#define CP_CLUSTER 0x80000000u
#define CLUSTER_LEN 12

/* Marcas por línea (integración con el shell, OSC 133 / 1337). */
enum { LINE_WRAPPED = 1, LINE_PROMPT = 2, LINE_OUTPUT = 4, LINE_MARK = 8, LINE_FAIL = 16, LINE_OK = 32 };

typedef struct {
    Cell *c;
    uint16_t len, cap;          /* celdas con contenido / reservadas */
    uint8_t flags;
    uint32_t time;              /* segundos desde la época, cuando se escribió por primera vez */
} Line;

/* Historia + pantalla en un anillo: la pantalla son las últimas `rows` líneas. */
typedef struct {
    Line *ring;
    int cap, count, head;
    long long dropped;          /* líneas descartadas por el límite de historia */
} Buffer;

typedef struct {
    long long line;             /* número absoluto de línea */
    int col, wc, hc;            /* posición y tamaño en celdas */
    int pw, ph;                 /* píxeles originales */
    uint32_t *px;               /* ARGB premultiplicado */
    uint32_t *scaled;
    int sw, sh;
} TermImage;

typedef struct { long long l0, l1; int c0, c1; } Match;   /* inclusive */

/* Perfil: lo que el usuario configura en la app (colores, fuente, cursor...). */
typedef struct {
    float font_size, line_spacing, padding;
    uint32_t palette[16];
    uint32_t fg, bg, bold, cursor, cursor_text, sel_bg, sel_fg, link, guide;
    float bg_opacity, min_contrast;
    int bold_bright, use_bold, use_italic, cursor_style, cursor_blink, cursor_guide;
    int blink_text, alt_esc, scrollback, mouse_reporting, timestamps, copy_on_select;
    int alt_scroll, has_sel_fg, has_cursor_text;
    char word_chars[64];
    int font[4], font_na[4];    /* fuente elegida y "no ASCII" (iTerm2): regular, negrita, itálica, ambas; -1 = la incluida */
    int ligatures;              /* aplicar calt/liga (JetBrains Mono, Fira Code...) */
    int bidi;                   /* árabe/hebreo en orden visual y con formas unidas (no en pantalla alternativa) */
    char font_family[64], na_family[64];
} TermProfile;

enum { CURSOR_BLOCK, CURSOR_UNDERLINE, CURSOR_BEAM };

/* Modos del terminal. */
enum {
    M_ORIGIN = 1 << 0, M_AUTOWRAP = 1 << 1, M_INSERT = 1 << 2, M_APPCURSOR = 1 << 3,
    M_APPKEYPAD = 1 << 4, M_HIDECURSOR = 1 << 5, M_BRACKETED = 1 << 6, M_FOCUS = 1 << 7,
    M_MOUSE_X10 = 1 << 8, M_MOUSE_BTN = 1 << 9, M_MOUSE_DRAG = 1 << 10, M_MOUSE_ANY = 1 << 11,
    M_MOUSE_SGR = 1 << 12, M_SYNC = 1 << 13, M_LNM = 1 << 14, M_REVERSE = 1 << 15,
    M_ALTSCROLL = 1 << 16, M_CURSOR_BLINK = 1 << 17, M_GRAPHEME = 1 << 18,
};
#define M_MOUSE (M_MOUSE_X10 | M_MOUSE_BTN | M_MOUSE_DRAG | M_MOUSE_ANY)

/* Eventos pendientes para JS. */
enum {
    EV_TITLE = 1 << 0, EV_CWD = 1 << 1, EV_BELL = 1 << 2, EV_EXIT = 1 << 3, EV_BADGE = 1 << 4,
    EV_NOTIFY = 1 << 5, EV_OUTPUT = 1 << 6, EV_PROFILE = 1 << 7, EV_ATTENTION = 1 << 8,
    EV_COPY = 1 << 9, EV_COMMAND = 1 << 10, EV_RUN = 1 << 11, EV_HOST = 1 << 12,
    EV_TMUX = 1 << 13, EV_TMUX_START = 1 << 14, EV_TMUX_END = 1 << 15, EV_RESIZE = 1 << 16,
};

typedef struct {
    int cx, cy, wrap, origin, gl, g[2];
    Cell pen;
} SavedCursor;

typedef struct Term {
    int cols, rows;
    Buffer main, alt, *buf;
    int cx, cy, wrap_pending;
    Cell pen;
    int top, bot;
    uint32_t modes;
    uint8_t *tabs;
    int g[2], gl;               /* juegos de caracteres: 0 ASCII, 1 gráficos DEC */
    int cursor_shape;           /* DECSCUSR: 0 = el del perfil */
    SavedCursor saved, saved_alt;
    uint32_t last_cp;

    /* parser */
    int state;
    int params[32], nparams, colon[32];
    char priv, inter[4];
    int ninter;
    char *str;                  /* OSC / DCS */
    size_t str_len, str_cap;
    uint32_t utf8_cp;
    int utf8_left;

    /* estado visible */
    char title[256], icon_title[256], cwd[1024], badge[256], notify[512], profile_req[128];
    char command[512];
    char host[256];             /* usuario@host informado por el shell (OSC 1337 RemoteHost / OSC 7) */
    uint32_t palette[256];
    uint32_t fg, bg, cursor_color;
    int fg_set, bg_set, cursor_set;
    char **links;
    int nlinks;
    TermImage *images;
    int nimages;
    long long cmd_prompt;       /* línea del prompt del comando en curso (OSC 133;C) */
    long long cmd_line;         /* dónde terminó el prompt (OSC 133;B): ahí empieza el comando */
    int cmd_col;
    char run_cmd[1024];         /* texto del último comando ejecutado */

    /* modo copia: cursor propio movido con el teclado (estilo vi) */
    int copy_active, copy_col, copy_sel;
    long long copy_line;

    /* selección (líneas absolutas del buffer activo) */
    int sel_active, sel_mode, sel_rect;
    long long sel_al, sel_bl;   /* ancla y extremo, tal como los marcó el mouse */
    int sel_ac, sel_bc;
    long long sel_sl, sel_el;   /* rango normalizado y expandido (palabra/línea) */
    int sel_sc, sel_ec;

    /* búsqueda */
    Match *matches;
    int nmatches, match_cur;

    /* vista y daño */
    int view;                   /* líneas desplazadas hacia atrás (0 = en vivo) */
    uint8_t *dirty;
    int all_dirty;
    uint32_t events;
    double sync_until;
    int focused;

    TermProfile *prof;
    float cell_w, cell_h;
    void (*respond)(struct Term *t, const char *s, size_t n);
    /* tmux -CC: kind 1 = empieza el modo control, 0 = una línea del protocolo, 2 = terminó */
    void (*tmux)(struct Term *t, int kind, const char *line, size_t n);
    void *user;
} Term;

/* vt.c */
uint32_t vt_base_cp(uint32_t cp);                        /* primer carácter del grafema */
int   vt_cluster_cps(uint32_t cp, uint32_t *out, int max); /* todos sus caracteres */
int   vt_init(Term *t, int cols, int rows, int scrollback, TermProfile *p);
void  vt_free(Term *t);
void  vt_write(Term *t, const uint8_t *data, size_t n);
void  vt_resize(Term *t, int cols, int rows);
void  vt_reset(Term *t);
void  vt_apply_palette(Term *t);
Line *vt_view_line(Term *t, int row, long long *abs);
long long vt_first_abs(const Term *t);
long long vt_total_lines(const Term *t);
Line *vt_abs_line(Term *t, long long abs);
long long vt_cursor_abs(const Term *t);
uint32_t vt_resolve(const Term *t, uint32_t color, int is_fg);
void  vt_scroll_view(Term *t, int delta);
void  vt_clear_scrollback(Term *t);
void  vt_mark_dirty(Term *t, int row);
/* selección */
void  vt_select_start(Term *t, long long line, int col, int mode, int rect);
void  vt_select_extend(Term *t, long long line, int col);
void  vt_select_all(Term *t);
void  vt_select_clear(Term *t);
int   vt_selected(const Term *t, long long line, int col);
char *vt_selection_text(Term *t);
char *vt_text_range(Term *t, long long from, long long to);
int   vt_select_last_output(Term *t);
/* búsqueda y marcas */
int   vt_search(Term *t, const char *query, int case_sensitive);
int   vt_search_move(Term *t, int dir);
void  vt_search_clear(Term *t);
int   vt_match_at(const Term *t, long long line, int col);   /* 0 no, 1 sí, 2 actual */
int   vt_jump_mark(Term *t, int dir);
void  vt_set_mark(Term *t);
char *vt_url_at(Term *t, long long line, int col);
char *vt_words(Term *t, const char *prefix, int max);
char *vt_cursor_word(Term *t);
char *vt_logical_text(Term *t);
int   vt_set_matches(Term *t, const int *trip, int n);
int   vt_highlight(Term *t, const char *text, int64_t fg, int64_t bg);
char *vt_serialize(Term *t, int max_lines, size_t *len);
void  vt_enter_tmux(Term *t);
int   vt_in_tmux(const Term *t);
int   vt_copy_mode(Term *t, int on);
int   vt_copy_key(Term *t, const char *key, int ctrl, char **yank);

/* hold.c: guardianes que mantienen vivos los shells si lord-server se reinicia */
int   hold_main(int argc, char **argv);
int   hold_spawn(int master, pid_t pid, char *key, size_t keylen);
void  hold_restore(void (*fn)(int fd, int ctrl, pid_t pid, const char *key));
void  hold_state_path(const char *key, char *out, size_t n);

/* pty.c */
pid_t pty_spawn(const char *cwd, int cols, int rows, int id, int *master);
void  pty_resize(int fd, int cols, int rows, int pw, int ph);
void  pty_set_integration(const char *dir);

/* term.c: sesiones */
typedef struct {                /* cuadro de Instant Replay */
    double time;                /* ms de reloj de pared */
    int cols, rows, cx, cy;
    Cell *cells;
} Frame;

typedef struct {
    int used, id, fd, node, alive, exit_code;
    pid_t pid;
    Term t;
    TermProfile prof;
    char *out;                  /* escritura pendiente hacia el shell */
    size_t out_len, out_cap;
    double last_output, last_input, last_event, last_click;
    int clicks, click_col;
    long long click_line;
    int dragging, mouse_btn;
    float gx, gy, gw, gh;       /* geometría del último dibujo */
    int drawn_cx, drawn_cy, blink_on;
    int pending_render;
    Frame *frames;              /* Instant Replay: anillo de pantallas pasadas */
    int nframes, frame_head, replay;
    size_t frame_bytes;
    double last_frame;
    int screen_changed;
    FILE *log;                  /* registro de la sesión (salida cruda) */
    char log_path[512];
    char key[48];               /* nombre del guardián (hold.c); "" si no tiene */
    int ctrl;                   /* conexión de control con el guardián, -1 = ninguna */
    int restored;               /* recuperada de un lord-server anterior */
    /* tmux -CC */
    int tmux;                   /* esta sesión es la "puerta" de un tmux en modo control */
    char *tlines;               /* líneas del protocolo pendientes para JS */
    size_t tl_n, tl_cap;
    int gateway, pane;          /* sesión virtual: id de la puerta y número de panel (%N) */
    int paused;                 /* virtual: la salida espera a que JS cargue el contenido */
    char *pend;
    size_t pend_n, pend_cap;
} Session;

void     term_init(const char *fonts_dir, const char *shell_dir);
Session *term_create(const char *cwd, int scrollback);
Session *term_get(int id);
void     term_close(Session *s);
void     term_input(Session *s, const char *data, size_t n);
void     term_key(Session *s, const char *key, const char *chr, const char *base, int mods);
void     term_paste(Session *s, const char *text);
void     term_focus(Session *s, int on);
int      term_mouse(Session *s, const char *type, float lx, float ly, int button, int mods);
void     term_wheel(Session *s, int lines, float lx, float ly, int mods);
int      term_replay(Session *s, int index);            /* -1 sale; devuelve el índice */
const char *term_log(Session *s, int on);
int      term_export(Session *s, char *path, size_t size);
int      term_pollfds(struct pollfd *p, int max, int *ids);
void     term_dispatch(int id, short revents);
int      term_damage(void);                      /* agrega el daño a ui.damage; 1 si hay */
double   term_timeout(double now);               /* ms hasta el próximo parpadeo, -1 nunca */
void     term_tick(double now);
typedef void (*TermEventFn)(int id, const char *type, const char *data);
void     term_events(TermEventFn fn);
void     term_paint(Canvas *c, int id, float x, float y, float w, float h, float opacity);
/* Orden visual de una fila con árabe/hebreo (term_paint.c); 0 si queda como está. */
int      term_row_visual(const TermProfile *p, const Term *t, const Line *l, int cols, int *vis, uint8_t *lv);
void     term_cell_at(Session *s, float lx, float ly, long long *line, int *col);
void     term_shutdown(int keep);                 /* keep: los shells siguen vivos para el próximo arranque */
void     term_restore(void);                       /* recupera las sesiones de los guardianes */
Session *term_tmux_pane(Session *gateway, int pane);  /* sesión virtual para un panel de tmux */
void     term_tmux_send(Session *gateway, const char *cmd, size_t n);
void     term_feed(Session *s, const char *data, size_t n);
void     term_tmux_ready(Session *s);
void     term_detach_all(void);
int      term_list(int *ids, int max);
const char *term_clipboard(void);
void     term_set_clipboard(const char *s, size_t n);
unsigned term_clipboard_gen(void);
/* De dónde vino lo último copiado ("terminal", "lordos" o el app_id de una app Wayland) y si es
 * sensible (contraseña: no va al historial). term_set_clipboard lo deja en "terminal", 0. */
void        term_clipboard_mark(const char *origin, int sensitive);
const char *term_clipboard_origin(void);
int         term_clipboard_sensitive(void);
int      term_mono_font(int bold, int italic);
int      term_profile_font(const TermProfile *p, int non_ascii, int bold, int italic);
int      term_set_fonts(TermProfile *p, const char *family, const char *non_ascii);
int      term_color_font(void);
int      term_fallback_font(int i);                 /* -1 al terminar la lista */
void     term_metrics(const TermProfile *p, float *cw, float *ch, float *baseline);
void     term_profile_defaults(TermProfile *p);
size_t   base64_decode(const char *in, size_t n, uint8_t *out);

#endif
