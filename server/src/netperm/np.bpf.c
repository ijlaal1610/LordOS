/* np.bpf.c — lord-netperm's cgroup BPF programs (see docs/FIREWALL.md, step 2).
 *
 * Restricted C for `clang -target bpf -O2` (Makefile, BPF=1). It doesn't use libbpf or its
 * headers: helpers are declared by number and the maps are symbols of the "maps" section that
 * build/bpf2c turns into relocations; lord-netperm creates the maps and fills in their fds.
 *
 * Programs (attached to the sessions' cgroup subtree, lordos.service):
 *   cgroup/connect4, connect6   TCP and connected UDP (also the UDP getaddrinfo uses for sorting)
 *   cgroup/sendmsg4, sendmsg6   unconnected UDP (sendto with an address)
 *   cgroup/bind4, bind6         "no incoming connections" and inbound rules (local port != 0)
 *   cgroup/sock_create          "no network": not even creating an AF_INET/AF_INET6 socket
 *   cgroup/dev                  "no camera", "no USB", "no screen capture" and "no microphone":
 *                               opening video devices, raw USB devices (/dev/bus/usb), hidraw,
 *                               ttyACM, ttyUSB, the framebuffer (by major) and ALSA capture
 *                               nodes (np_snd: for an app's own scope every ALSA node not known
 *                               to be a non-capture one); every other device stays allowed
 *   cgroup/lsm_socket_create    "no Bluetooth": socket(AF_BLUETOOTH) (a per-cgroup LSM program,
 *                               BPF_LSM_CGROUP: the inet sock_create hook never sees that family)
 *
 * The same programs are also attached to the user@<uid>.service of each LordOS session, to
 * close the `systemd-run --user` escape (the process ends up there, outside lordos.service). A
 * process that isn't in any app's scope (ancestor at level cfg.level) falls into the strict union
 * of the restrictions of the user's restricted apps (np_union, keyed by the user@<uid> at level
 * cfg.user_level): it's never less restricted than its own policy.
 *
 * Each one: looks up the app by the process's scope (ancestor at level cfg.level) or, failing
 * that, by the user@ union (level cfg.user_level), decides with
 * np_decide (np_shared.h, the same thing tests/npcompile_test.c tests) and returns 1 (allow) or
 * 0 (the kernel returns EPERM to the app). It counts the connection and notifies lord-netperm over
 * the ring buffer only if it's new (np_seen, 5 s). Nothing per packet: only per connection, bind or
 * sendto. A UDP connect() counts only the first time within 5 s, like a sendto: getaddrinfo does one
 * per address to sort them (RFC 6724) and they aren't real connections. */
#define NP_BPF 1
#include <linux/bpf.h>
#include "netperm/np_shared.h"

#define SEC(n) __attribute__((section(n), used))

/* helpers by number (include/uapi/linux/bpf.h, __BPF_FUNC_MAPPER) */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, unsigned long long flags) = (void *)2;
static unsigned long long (*bpf_ktime_get_ns)(void) = (void *)5;
static unsigned long long (*bpf_get_current_ancestor_cgroup_id)(int level) = (void *)123;
static long (*bpf_ringbuf_output)(void *ringbuf, void *data, unsigned long long size, unsigned long long flags) = (void *)130;

/* the maps: only their names (lord-netperm defines them, netperm.c) */
char np_cfg[4] SEC("maps");
char np_apps[4] SEC("maps");
char np_union[4] SEC("maps");
char np_lpm4[4] SEC("maps");
char np_lpm6[4] SEC("maps");
char np_seen[4] SEC("maps");
char np_counts[4] SEC("maps");
char np_events[4] SEC("maps");
char np_snd[4] SEC("maps");

#define AF_INET  2
#define AF_INET6 10
#define AF_BLUETOOTH 31
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

NP_INLINE int proto_of(np_u32 p) {
    return p == IPPROTO_TCP ? NP_P_TCP : p == IPPROTO_UDP ? NP_P_UDP : NP_P_OTHER;
}

/* Looks up the current process's policy: first its app scope (np_apps, ancestor at level
 * cfg->level); if it isn't there (e.g. it moved out to user@<uid> with systemd-run), the strict
 * union of the user's restricted apps (np_union, ancestor at level cfg->user_level). Returns the
 * NpApp and leaves in *cgid the key to count and report with (the scope or the user@). 0 if it doesn't apply. */
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

/* fam 0: unknown address. addr: 16 bytes (IPv4 in the first 4). always_count: connect and
 * bind always count; sendto only the first time within NP_REPEAT_NS. */
NP_INLINE int handle(int fam, np_u8 *addr, int port, int proto, int dir, int always_count) {
    np_u32 zero = 0;
    NpCfg *cfg = bpf_map_lookup_elem(&np_cfg, &zero);
    if (!cfg || !cfg->on) return 1;
    np_u64 cgid = 0;
    NpApp *app = find_app(cfg, &cgid);
    if (!app) return 1;                                 /* neither an app scope nor a restricted user@ */

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
    np_u32 v = ctx->user_ip4;                           /* network order: the bytes in memory */
    a[0] = (np_u8)v; a[1] = (np_u8)(v >> 8); a[2] = (np_u8)(v >> 16); a[3] = (np_u8)(v >> 24);
    return 4;
}

/* IPv6; a mapped IPv4 (::ffff:a.b.c.d) comes back as IPv4, same as in fw_decide. */
NP_INLINE int addr6(struct bpf_sock_addr *ctx, np_u8 *a) {
    np_u32 w0 = ctx->user_ip6[0], w1 = ctx->user_ip6[1], w2 = ctx->user_ip6[2], w3 = ctx->user_ip6[3];
    if (w0 == 0 && w1 == 0 && w2 == 0xffff0000u) {      /* 00 00 ff ff in memory */
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

/* bind: the remote address isn't known. Only if it's loopback is the connection surely local
 * (like a fw_decide query with that address); otherwise "unknown" = internet. Port 0
 * (ephemeral, used by clients) isn't checked. */
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

/* "no network": the app can't create internet sockets (nor ICMP, which doesn't go through connect). */
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
    handle(0, a, 0, proto_of(sk->protocol), NP_D_OUT, 1);   /* counts and reports (denies because of the restriction) */
    return 0;
}

/* A denied device open or Bluetooth socket: reported once per 5 s (np_seen). Not counted: the
 * Monitor's allowed/denied counters are about connections. */
NP_INLINE void report_dev(np_u64 cgid, int dir, np_u32 major, np_u32 minor, np_u32 flag) {
    NpSeenKey sk;
    __builtin_memset(&sk, 0, sizeof sk);
    sk.cgid = cgid;
    sk.addr[0] = (np_u8)minor; sk.addr[1] = (np_u8)(minor >> 8); sk.addr[2] = (np_u8)(minor >> 16); sk.addr[3] = (np_u8)(minor >> 24);
    sk.port = (np_u16)major;
    sk.dir = (np_u8)dir; sk.verdict = 1;
    np_u64 now = bpf_ktime_get_ns();
    np_u64 *last = bpf_map_lookup_elem(&np_seen, &sk);
    if (last && now - *last <= NP_REPEAT_NS) return;
    bpf_map_update_elem(&np_seen, &sk, &now, BPF_ANY);
    NpEvent ev;
    __builtin_memset(&ev, 0, sizeof ev);
    ev.cgid = cgid;
    ev.ts = now;
    for (int i = 0; i < 4; i++) ev.addr[i] = sk.addr[i];
    ev.port = (np_u16)major;
    ev.proto = NP_P_OTHER; ev.dir = (np_u8)dir; ev.verdict = NP_V_DENY;
    ev.reason = NP_BY_RESTRICTED; ev.record = 1;
    ev.flag = flag;
    bpf_ringbuf_output(&np_events, &ev, sizeof ev, 0);
}

/* "no camera" / "no USB": the device controller of cgroup v2 is this kind of program. It runs on
 * open() and mknod() of a device node; 1 = allow, 0 = EPERM. Programs attached higher up (systemd's
 * DeviceAllow) still apply: the kernel allows only what all of them allow. Only the devices of
 * np_dev_restriction are ever denied, and only to a restricted app. */
/* Under user@<uid> the union applies to every process, PipeWire and WirePlumber included: the
 * user owns that subtree and can move any of their processes into any of its cgroups, so an
 * exemption by cgroup would be an open door. The cost (docs/FIREWALL.md): while a camera- or
 * Bluetooth-restricted app runs, the user's own services can't open a new camera or create a new
 * Bluetooth socket either; what they already have open keeps working. */
SEC("cgroup/dev") int np_dev(struct bpf_cgroup_dev_ctx *ctx) {
    np_u32 zero = 0;
    NpCfg *cfg = bpf_map_lookup_elem(&np_cfg, &zero);
    if (!cfg || !cfg->on) return 1;
    np_u32 need = np_dev_restriction(ctx->access_type & 0xffff, ctx->major, cfg->hidraw_major);
    int snd = !need && (ctx->access_type & 0xffff) == NP_DEV_CHAR && ctx->major == NP_MAJOR_SND;
    if (!need && !snd) return 1;
    np_u64 cgid = 0;
    NpApp *app = find_app(cfg, &cgid);
    if (!app) return 1;
    if (snd) {
        /* Recording straight from the card. In the app's own scope this fails closed: an ALSA node
         * the last scan didn't classify (a USB microphone plugged in since) counts as capture.
         * Outside it only the known capture nodes count (lord-netperm keeps no-mic out of the
         * user@ union today, so this is for a union that ever carries it). */
        np_u32 minor = ctx->minor;
        np_u32 *kind = bpf_map_lookup_elem(&np_snd, &minor);
        np_u64 sid = bpf_get_current_ancestor_cgroup_id((int)cfg->level);
        int own = sid && sid == cgid;
        if (kind ? *kind == NP_SND_CAPTURE : own) need = NP_R_NO_MIC;
        else return 1;
    }
    if (!(app->flags & need)) return 1;
    report_dev(cgid, NP_D_DEV, ctx->major, ctx->minor, need);
    return 0;
}

/* "no Bluetooth": socket(AF_BLUETOOTH, ...) of a restricted app. A cgroup LSM program on the
 * socket_create hook (BPF_LSM_CGROUP, kernel >= 6.0 with the bpf LSM active); ctx are the hook's
 * arguments (family, type, protocol, kern). kern = 1 is the kernel's own socket: never touched. */
SEC("cgroup/lsm_socket_create") int np_bt_socket(unsigned long long *ctx) {
    int family = (int)ctx[0];
    int kern = (int)ctx[3];
    if (family != AF_BLUETOOTH || kern) return 1;
    np_u32 zero = 0;
    NpCfg *cfg = bpf_map_lookup_elem(&np_cfg, &zero);
    if (!cfg || !cfg->on) return 1;
    np_u64 cgid = 0;
    NpApp *app = find_app(cfg, &cgid);
    if (!app || !(app->flags & NP_R_NO_BLUETOOTH)) return 1;
    report_dev(cgid, NP_D_BT, 0, 0, NP_R_NO_BLUETOOTH);
    return 0;
}
