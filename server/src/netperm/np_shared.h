/* np_shared.h — lo que comparten los programas BPF de lord-netperm (np.bpf.c), el que compila las
 * reglas a mapas (npcompile.c) y su prueba (tests/npcompile_test.c). Ver docs/FIREWALL.md, paso 2.
 *
 * Se compila igual con `clang -target bpf` (sin libc) y con el compilador del sistema: solo tipos
 * de ancho fijo y funciones inline sin llamadas. La decisión del kernel (np_decide) es esta misma
 * función, así la prueba sin kernel compara exactamente lo que va a correr adentro contra
 * fw_decide (la referencia del paso 1).
 *
 * Mapas (los crea lord-netperm; los programas los ven por nombre):
 *   np_cfg     ARRAY  [0] → NpCfg                   nivel de la scope de las apps, encendido
 *   np_apps    HASH   id de cgroup (u64) → NpApp    restricciones, confiable, modo, lista genérica
 *   np_union   HASH   id de user@<uid> (u64) → NpApp  unión estricta de las apps restringidas del usuario
 *   np_lpm4    LPM    (idx de app, IPv4/n) → NpList reglas de red y de dominio, ordenadas
 *   np_lpm6    LPM    (idx de app, IPv6/n) → NpList
 *   np_seen    LRU    NpSeenKey → u64 ns            para avisar solo las conexiones nuevas
 *   np_counts  HASH   id de cgroup → NpCount        permitidas / denegadas
 *   np_events  RINGBUF                              NpEvent hacia lord-netperm
 */
#ifndef LORD_NP_SHARED_H
#define LORD_NP_SHARED_H

#ifdef NP_BPF
typedef unsigned char      np_u8;
typedef unsigned short     np_u16;
typedef unsigned int       np_u32;
typedef unsigned long long np_u64;
#define NP_INLINE static inline __attribute__((always_inline))
#else
#include <stdint.h>
typedef uint8_t  np_u8;
typedef uint16_t np_u16;
typedef uint32_t np_u32;
typedef uint64_t np_u64;
#define NP_INLINE static inline
#endif

#define NP_LIST_MAX   16        /* reglas por dirección o red (las de las redes que la contienen incluidas) */
#define NP_GEN_MAX    16        /* reglas lan / internet / any de una app */

#define NP_MAX_APPS   8192      /* cgroups con política */
#define NP_MAX_LPM    65536     /* entradas de cada trie */
#define NP_MAX_SEEN   16384
#define NP_RINGBUF    (256u << 10)
#define NP_REPEAT_NS  (5ull * 1000000000ull)   /* una conexión repetida se vuelve a avisar a los 5 s */

/* restricciones (los mismos bits que FW_R_* de fw/rules.h) */
#define NP_R_NO_INTERNET  (1u << 0)
#define NP_R_NO_LAN       (1u << 1)
#define NP_R_NO_NETWORK   (1u << 2)
#define NP_R_NO_INCOMING  (1u << 3)

enum { NP_P_ANY = 0, NP_P_TCP = 1, NP_P_UDP = 2, NP_P_OTHER = 3 };   /* como FwProto (+ otro) */
enum { NP_D_OUT = 0, NP_D_IN = 1 };
enum { NP_V_ALLOW = 0, NP_V_DENY = 1 };
enum { NP_K_RULE = 0, NP_K_ANY = 1, NP_K_LAN = 2, NP_K_INET = 3 };   /* NpEnt.kind */
enum { NP_M_SILENT = 0, NP_M_OFF = 2 };                              /* "preguntar" va como silencioso */
enum { NP_BY_RESTRICTED = 0, NP_BY_RULE = 1, NP_BY_TRUSTED = 2, NP_BY_MODE = 3 };

typedef struct {
    np_u16 port_lo, port_hi;    /* 0..65535 = cualquiera */
    np_u8  proto;               /* NP_P_ANY / TCP / UDP */
    np_u8  dir;                 /* NP_D_OUT / NP_D_IN */
    np_u8  verdict;             /* NP_V_ALLOW / NP_V_DENY */
    np_u8  kind;                /* en la lista genérica: NP_K_ANY / LAN / INET; en el trie: NP_K_RULE */
    np_u32 rule_id;
} NpEnt;

typedef struct {
    np_u32 n;
    np_u32 pad;
    NpEnt  e[NP_LIST_MAX];      /* en orden de precedencia: gana la primera que coincide */
} NpList;

typedef struct {
    np_u32 idx;                 /* índice de la app en los tries (por usuario y app) */
    np_u32 flags;               /* NP_R_* */
    np_u8  trusted;
    np_u8  mode;                /* NP_M_* */
    np_u8  has4, has6;          /* hay entradas en np_lpm4 / np_lpm6 */
    np_u32 ngen;
    NpEnt  gen[NP_GEN_MAX];     /* reglas lan / internet / any, en orden */
} NpApp;

typedef struct { np_u32 prefixlen; np_u8 data[8]; } NpLpm4Key;    /* idx (big endian) + IPv4 */
typedef struct { np_u32 prefixlen; np_u8 data[20]; } NpLpm6Key;   /* idx (big endian) + IPv6 */

typedef struct {
    np_u32 level;               /* profundidad de las scopes de apps (bpf_get_current_ancestor_cgroup_id) */
    np_u32 on;                  /* 0: todo se permite sin mirar nada */
    np_u32 user_level;          /* profundidad de user@<uid>.service; 0: no mirar la unión de user@ */
} NpCfg;

typedef struct {
    np_u64 cgid;
    np_u8  addr[16];
    np_u16 port;
    np_u8  fam, proto, dir, verdict;
    np_u8  pad[2];
} NpSeenKey;

typedef struct { np_u64 allowed, denied; } NpCount;

typedef struct {
    np_u64 cgid;
    np_u64 ts;                  /* ns de bpf_ktime_get_ns */
    np_u8  addr[16];            /* IPv4 en los 4 primeros bytes */
    np_u16 port;
    np_u8  fam, proto, dir, verdict, reason, record;
    np_u32 rule_id;
    np_u32 flag;
} NpEvent;

typedef struct {
    int    verdict, reason, record;
    np_u32 rule_id, flag;
} NpRes;

/* LAN, igual que fw_addr_is_lan (las direcciones van en orden de red). */
NP_INLINE int np_is_lan4(const np_u8 *a) {
    np_u8 b0 = a[0], b1 = a[1];
    if (b0 == 0 || b0 == 10 || b0 == 127) return 1;
    if (b0 == 100 && (b1 & 0xc0) == 64) return 1;
    if (b0 == 169 && b1 == 254) return 1;
    if (b0 == 172 && (b1 & 0xf0) == 16) return 1;
    if (b0 == 192 && b1 == 168) return 1;
    if ((b0 & 0xf0) == 224) return 1;
    if (b0 == 255 && b1 == 255 && a[2] == 255 && a[3] == 255) return 1;
    return 0;
}

NP_INLINE int np_is_lan6(const np_u8 *a) {
    int zero = 1;
    for (int i = 0; i < 15; i++) if (a[i]) zero = 0;
    if (zero && (a[15] == 0 || a[15] == 1)) return 1;           /* :: y ::1 */
    if ((a[0] & 0xfe) == 0xfc) return 1;                         /* fc00::/7 */
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 1;         /* fe80::/10 */
    if (a[0] == 0xff) return 1;                                  /* ff00::/8 */
    return 0;
}

NP_INLINE int np_ent_match(const NpEnt *e, int dir, int proto, int port) {
    if (e->dir != dir) return 0;
    if (e->proto != NP_P_ANY && e->proto != proto) return 0;
    if (port < e->port_lo || port > e->port_hi) return 0;
    return 1;
}

/* La decisión, en el mismo orden que fw_decide: restricciones, reglas (primero la lista del trie,
 * que ya trae dominios y redes ordenados, después lan/internet/any), confiable y modo.
 *   lan: 1 LAN, 0 internet, -1 dirección desconocida (bind a una dirección que no es local). */
NP_INLINE void np_decide(const NpApp *a, const NpList *l, int dir, int proto, int port, int lan, NpRes *r) {
    r->verdict = NP_V_DENY; r->reason = NP_BY_RESTRICTED; r->record = 1; r->rule_id = 0; r->flag = 0;
    np_u32 f = a->flags;
    if (f & NP_R_NO_NETWORK) { r->flag = NP_R_NO_NETWORK; return; }
    if (dir == NP_D_IN && (f & NP_R_NO_INCOMING)) { r->flag = NP_R_NO_INCOMING; return; }
    if ((f & NP_R_NO_INTERNET) && lan != 1) { r->flag = NP_R_NO_INTERNET; return; }
    if ((f & NP_R_NO_LAN) && lan == 1) { r->flag = NP_R_NO_LAN; return; }
    r->record = 0;
    if (l) {
        np_u32 n = l->n;
        for (int i = 0; i < NP_LIST_MAX; i++) {
            if ((np_u32)i >= n) break;
            const NpEnt *e = &l->e[i];
            if (np_ent_match(e, dir, proto, port)) {
                r->verdict = e->verdict; r->reason = NP_BY_RULE; r->rule_id = e->rule_id;
                return;
            }
        }
    }
    np_u32 ng = a->ngen;
    for (int i = 0; i < NP_GEN_MAX; i++) {
        if ((np_u32)i >= ng) break;
        const NpEnt *e = &a->gen[i];
        if (e->kind == NP_K_LAN && lan != 1) continue;
        if (e->kind == NP_K_INET && lan != 0) continue;
        if (np_ent_match(e, dir, proto, port)) {
            r->verdict = e->verdict; r->reason = NP_BY_RULE; r->rule_id = e->rule_id;
            return;
        }
    }
    r->verdict = NP_V_ALLOW;
    if (a->trusted) { r->reason = NP_BY_TRUSTED; return; }
    r->reason = NP_BY_MODE;
    r->record = a->mode == NP_M_SILENT;
}

#endif
