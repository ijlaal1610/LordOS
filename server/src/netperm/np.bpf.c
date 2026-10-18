/* np.bpf.c — los programas BPF de cgroup de lord-netperm (ver docs/FIREWALL.md, paso 2).
 *
 * C restringido para `clang -target bpf -O2` (Makefile, BPF=1). No usa libbpf ni sus cabeceras:
 * los ayudantes se declaran por número y los mapas son símbolos de la sección "maps" que
 * build/bpf2c convierte en reubicaciones; lord-netperm crea los mapas y pone sus fds.
 *
 * Programas (se enganchan en el subárbol de cgroup de las sesiones, lordos.service):
 *   cgroup/connect4, connect6   TCP y UDP conectado (también la UDP que usa getaddrinfo para ordenar)
 *   cgroup/sendmsg4, sendmsg6   UDP sin conectar (sendto con dirección)
 *   cgroup/bind4, bind6         "sin conexiones entrantes" y reglas de entrada (puerto local != 0)
 *   cgroup/sock_create          "sin red": ni siquiera crear un socket AF_INET/AF_INET6
 *
 * Los mismos programas se enganchan además en el user@<uid>.service de cada sesión de LordOS, para
 * cerrar la fuga de `systemd-run --user` (el proceso termina ahí, fuera de lordos.service). Un
 * proceso que no está en la scope de ninguna app (antepasado de nivel cfg.level) cae en la unión
 * estricta de las restricciones de las apps restringidas del usuario (np_union, con la clave del
 * user@<uid> en el nivel cfg.user_level): nunca es menos restrictivo que su propia política.
 *
 * Cada uno: busca la app por la scope del proceso (antepasado de nivel cfg.level) o, si no, por la
 * unión de user@ (nivel cfg.user_level), decide con
 * np_decide (np_shared.h, lo mismo que prueba tests/npcompile_test.c) y devuelve 1 (permitir) o
 * 0 (el kernel le devuelve EPERM a la app). Cuenta la conexión y avisa a lord-netperm por el anillo
 * solo si es nueva (np_seen, 5 s). Nada por paquete: solo por conexión, bind o sendto. Un connect()
 * de UDP cuenta solo la primera vez en 5 s, como un sendto: getaddrinfo hace uno por cada dirección
 * para ordenarlas (RFC 6724) y no son conexiones de verdad. */
#define NP_BPF 1
#include <linux/bpf.h>
#include "netperm/np_shared.h"

#define SEC(n) __attribute__((section(n), used))

/* ayudantes por número (include/uapi/linux/bpf.h, __BPF_FUNC_MAPPER) */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, unsigned long long flags) = (void *)2;
static unsigned long long (*bpf_ktime_get_ns)(void) = (void *)5;
static unsigned long long (*bpf_get_current_ancestor_cgroup_id)(int level) = (void *)123;
static long (*bpf_ringbuf_output)(void *ringbuf, void *data, unsigned long long size, unsigned long long flags) = (void *)130;

/* los mapas: solo sus nombres (los define lord-netperm, netperm.c) */
char np_cfg[4] SEC("maps");
char np_apps[4] SEC("maps");
char np_union[4] SEC("maps");
char np_lpm4[4] SEC("maps");
char np_lpm6[4] SEC("maps");
char np_seen[4] SEC("maps");
char np_counts[4] SEC("maps");
char np_events[4] SEC("maps");

#define AF_INET  2
#define AF_INET6 10
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

NP_INLINE int proto_of(np_u32 p) {
    return p == IPPROTO_TCP ? NP_P_TCP : p == IPPROTO_UDP ? NP_P_UDP : NP_P_OTHER;
}

/* Busca la política del proceso actual: primero su scope de app (np_apps, antepasado de nivel
 * cfg->level); si no está (por ejemplo salió a user@<uid> con systemd-run), la unión estricta de
 * las apps restringidas del usuario (np_union, antepasado de nivel cfg->user_level). Devuelve la
 * NpApp y deja en *cgid la clave con la que contar y avisar (la scope o el user@). 0 si no aplica. */
NP_INLINE NpApp *find_app(NpCfg *cfg, np_u64 *cgid) {
    np_u64 id = bpf_get_current_ancestor_cgroup_id((int)cfg->level);
    if (id) {
        NpApp *app = bpf_map_lookup_elem(&np_apps, &id);
        if (app) { *cgid = id; return app; }
    }
    if (cfg->user_level) {
        np_u64 uid = bpf_get_current_ancestor_cgroup_id((int)cfg->user_level);
        if (uid) {
            NpApp *app = bpf_map_lookup_elem(&np_union, &uid);
            if (app) { *cgid = uid; return app; }
        }
    }
    return 0;
}

NP_INLINE void count(np_u64 cgid, int deny) {
    NpCount *c = bpf_map_lookup_elem(&np_counts, &cgid);
    if (!c) {
        NpCount z = { 0, 0 };
        bpf_map_update_elem(&np_counts, &cgid, &z, BPF_NOEXIST);
        c = bpf_map_lookup_elem(&np_counts, &cgid);
        if (!c) return;
    }
    if (deny) __sync_fetch_and_add(&c->denied, 1);
    else __sync_fetch_and_add(&c->allowed, 1);
}

/* fam 0: dirección desconocida. addr: 16 bytes (IPv4 en los 4 primeros). always_count: connect y
 * bind cuentan siempre; sendto solo la primera vez en NP_REPEAT_NS. */
NP_INLINE int handle(int fam, np_u8 *addr, int port, int proto, int dir, int always_count) {
    np_u32 zero = 0;
    NpCfg *cfg = bpf_map_lookup_elem(&np_cfg, &zero);
    if (!cfg || !cfg->on) return 1;
    np_u64 cgid = 0;
    NpApp *app = find_app(cfg, &cgid);
    if (!app) return 1;                                 /* ni una scope de app ni user@ restringido */

    int lan = -1;
    NpList *l = 0;
    if (fam == 4) {
        lan = np_is_lan4(addr);
        if (app->has4) {
            NpLpm4Key k;
            k.prefixlen = 64;
            k.data[0] = (np_u8)(app->idx >> 24); k.data[1] = (np_u8)(app->idx >> 16);
            k.data[2] = (np_u8)(app->idx >> 8);  k.data[3] = (np_u8)app->idx;
            k.data[4] = addr[0]; k.data[5] = addr[1]; k.data[6] = addr[2]; k.data[7] = addr[3];
            l = bpf_map_lookup_elem(&np_lpm4, &k);
        }
    } else if (fam == 6) {
        lan = np_is_lan6(addr);
        if (app->has6) {
            NpLpm6Key k;
            k.prefixlen = 160;
            k.data[0] = (np_u8)(app->idx >> 24); k.data[1] = (np_u8)(app->idx >> 16);
            k.data[2] = (np_u8)(app->idx >> 8);  k.data[3] = (np_u8)app->idx;
            for (int i = 0; i < 16; i++) k.data[4 + i] = addr[i];
            l = bpf_map_lookup_elem(&np_lpm6, &k);
        }
    }
    NpRes r;
    np_decide(app, l, dir, proto, port, lan, &r);
    int deny = r.verdict == NP_V_DENY;

    NpSeenKey sk;
    __builtin_memset(&sk, 0, sizeof sk);
    sk.cgid = cgid;
    for (int i = 0; i < 16; i++) sk.addr[i] = addr[i];
    sk.port = (np_u16)port;
    sk.fam = (np_u8)fam; sk.proto = (np_u8)proto; sk.dir = (np_u8)dir; sk.verdict = (np_u8)deny;
    np_u64 now = bpf_ktime_get_ns();
    np_u64 *last = bpf_map_lookup_elem(&np_seen, &sk);
    int fresh = !last || now - *last > NP_REPEAT_NS;
    if (always_count || fresh) count(cgid, deny);
    if (fresh) {
        bpf_map_update_elem(&np_seen, &sk, &now, BPF_ANY);
        NpEvent ev;
        __builtin_memset(&ev, 0, sizeof ev);
        ev.cgid = cgid;
        ev.ts = now;
        for (int i = 0; i < 16; i++) ev.addr[i] = addr[i];
        ev.port = (np_u16)port;
        ev.fam = (np_u8)fam; ev.proto = (np_u8)proto; ev.dir = (np_u8)dir; ev.verdict = (np_u8)r.verdict;
        ev.reason = (np_u8)r.reason; ev.record = (np_u8)r.record;
        ev.rule_id = r.rule_id; ev.flag = r.flag;
        bpf_ringbuf_output(&np_events, &ev, sizeof ev, 0);
    }
    return !deny;
}

NP_INLINE int addr4(struct bpf_sock_addr *ctx, np_u8 *a) {
    np_u32 v = ctx->user_ip4;                           /* orden de red: los bytes en memoria */
    a[0] = (np_u8)v; a[1] = (np_u8)(v >> 8); a[2] = (np_u8)(v >> 16); a[3] = (np_u8)(v >> 24);
    return 4;
}

/* IPv6; una IPv4 mapeada (::ffff:a.b.c.d) vuelve como IPv4, igual que en fw_decide. */
NP_INLINE int addr6(struct bpf_sock_addr *ctx, np_u8 *a) {
    np_u32 w0 = ctx->user_ip6[0], w1 = ctx->user_ip6[1], w2 = ctx->user_ip6[2], w3 = ctx->user_ip6[3];
    if (w0 == 0 && w1 == 0 && w2 == 0xffff0000u) {      /* 00 00 ff ff en memoria */
        a[0] = (np_u8)w3; a[1] = (np_u8)(w3 >> 8); a[2] = (np_u8)(w3 >> 16); a[3] = (np_u8)(w3 >> 24);
        return 4;
    }
    np_u32 w[4] = { w0, w1, w2, w3 };
    for (int i = 0; i < 4; i++) {
        a[i * 4] = (np_u8)w[i]; a[i * 4 + 1] = (np_u8)(w[i] >> 8);
        a[i * 4 + 2] = (np_u8)(w[i] >> 16); a[i * 4 + 3] = (np_u8)(w[i] >> 24);
    }
    return 6;
}

NP_INLINE int port_of(struct bpf_sock_addr *ctx) { return __builtin_bswap16((np_u16)ctx->user_port); }

SEC("cgroup/connect4") int np_connect4(struct bpf_sock_addr *ctx) {
    np_u8 a[16] = { 0 };
    int fam = addr4(ctx, a);
    int proto = proto_of(ctx->protocol);
    return handle(fam, a, port_of(ctx), proto, NP_D_OUT, proto != NP_P_UDP);
}

SEC("cgroup/connect6") int np_connect6(struct bpf_sock_addr *ctx) {
    np_u8 a[16] = { 0 };
    int fam = addr6(ctx, a);
    int proto = proto_of(ctx->protocol);
    return handle(fam, a, port_of(ctx), proto, NP_D_OUT, proto != NP_P_UDP);
}

SEC("cgroup/sendmsg4") int np_sendmsg4(struct bpf_sock_addr *ctx) {
    np_u8 a[16] = { 0 };
    int fam = addr4(ctx, a);
    return handle(fam, a, port_of(ctx), NP_P_UDP, NP_D_OUT, 0);
}

SEC("cgroup/sendmsg6") int np_sendmsg6(struct bpf_sock_addr *ctx) {
    np_u8 a[16] = { 0 };
    int fam = addr6(ctx, a);
    return handle(fam, a, port_of(ctx), NP_P_UDP, NP_D_OUT, 0);
}

/* bind: la dirección remota no se sabe. Solo si es de loopback la conexión es seguro local
 * (como una consulta de fw_decide con esa dirección); si no, "desconocida" = internet. El puerto
 * 0 (efímero, lo usan los clientes) no se mira. */
SEC("cgroup/bind4") int np_bind4(struct bpf_sock_addr *ctx) {
    int port = port_of(ctx);
    if (!port) return 1;
    np_u8 a[16] = { 0 };
    addr4(ctx, a);
    int fam = a[0] == 127 ? 4 : 0;
    if (!fam) a[0] = a[1] = a[2] = a[3] = 0;
    return handle(fam, a, port, proto_of(ctx->protocol), NP_D_IN, 1);
}

SEC("cgroup/bind6") int np_bind6(struct bpf_sock_addr *ctx) {
    int port = port_of(ctx);
    if (!port) return 1;
    np_u8 a[16] = { 0 };
    int fam = addr6(ctx, a);
    int local = fam == 4 ? a[0] == 127 : np_is_lan6(a) && a[0] == 0 && a[15] == 1;   /* ::1 */
    if (!local) {
        for (int i = 0; i < 16; i++) a[i] = 0;
        fam = 0;
    }
    return handle(fam, a, port, proto_of(ctx->protocol), NP_D_IN, 1);
}

/* "sin red": la app no puede crear sockets de internet (tampoco ICMP, que no pasa por connect). */
SEC("cgroup/sock_create") int np_sock_create(struct bpf_sock *sk) {
    np_u32 f = sk->family;
    if (f != AF_INET && f != AF_INET6) return 1;
    np_u32 zero = 0;
    NpCfg *cfg = bpf_map_lookup_elem(&np_cfg, &zero);
    if (!cfg || !cfg->on) return 1;
    np_u64 cgid = bpf_get_current_ancestor_cgroup_id((int)cfg->level);
    if (!cgid) return 1;
    NpApp *app = bpf_map_lookup_elem(&np_apps, &cgid);
    if (!app || !(app->flags & NP_R_NO_NETWORK)) return 1;
    np_u8 a[16] = { 0 };
    handle(0, a, 0, proto_of(sk->protocol), NP_D_OUT, 1);   /* cuenta y avisa (niega por la restricción) */
    return 0;
}
