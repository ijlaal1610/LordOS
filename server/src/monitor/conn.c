/* conn.c — conexiones de red del usuario para el Monitor (ver docs/MONITOR.md).
 *
 * Igual que `ss`: se piden al kernel por sock_diag (NETLINK_SOCK_DIAG) todos los sockets TCP y
 * UDP, IPv4 e IPv6, con tcp_info para los bytes enviados y recibidos. sock_diag no necesita
 * privilegios. A cada socket del usuario se le busca el proceso dueño leyendo los fd de sus
 * propios procesos (/proc/<pid>/fd → "socket:[inodo]"), con una caché para no recorrerlos en
 * cada muestra. De los sockets de otros usuarios solo se cuenta cuántos hay.
 *
 * El país sale de la base local (mmdb.c). Ninguna dirección sale del equipo. */
#include "monpriv.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_SOCK 4096
#define MAP_CAP 16384                   /* potencia de 2 */

typedef struct {
    uint32_t inode, uid;
    uint8_t fam, proto, state;
    uint8_t src[16], dst[16];
    uint16_t sport, dport;
    uint64_t sent, recv;
    int bytes;                          /* sent/recv válidos (TCP con tcp_info) */
} Sock;

static Sock socks[MAX_SOCK];
static int nsock;

/* ---- sock_diag ---------------------------------------------------------------------------- */

static int dump(int fd, int fam, int proto) {
    struct { struct nlmsghdr nh; struct inet_diag_req_v2 r; } req;
    memset(&req, 0, sizeof req);
    req.nh.nlmsg_len = sizeof req;
    req.nh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = (uint32_t)(fam * 1000 + proto);
    req.r.sdiag_family = (uint8_t)fam;
    req.r.sdiag_protocol = (uint8_t)proto;
    req.r.idiag_states = ~0u;
    if (proto == IPPROTO_TCP) req.r.idiag_ext = 1 << (INET_DIAG_INFO - 1);
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
    if (sendto(fd, &req, sizeof req, 0, (struct sockaddr *)&sa, sizeof sa) < 0) return -1;
    static char buf[65536] __attribute__((aligned(8)));
    for (;;) {
        ssize_t got = recv(fd, buf, sizeof buf, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        int n = (int)got;
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf; NLMSG_OK(h, n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE) return 0;
            if (h->nlmsg_type == NLMSG_ERROR) return -1;
            if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY || h->nlmsg_len < NLMSG_LENGTH(sizeof(struct inet_diag_msg))) continue;
            const struct inet_diag_msg *m = NLMSG_DATA(h);
            if (nsock >= MAX_SOCK) continue;
            Sock *s = &socks[nsock];
            memset(s, 0, sizeof *s);
            s->inode = m->idiag_inode;
            s->uid = m->idiag_uid;
            s->fam = m->idiag_family == AF_INET6 ? 6 : 4;
            s->proto = (uint8_t)proto;
            s->state = m->idiag_state;
            s->sport = ntohs(m->id.idiag_sport);
            s->dport = ntohs(m->id.idiag_dport);
            memcpy(s->src, m->id.idiag_src, 16);
            memcpy(s->dst, m->id.idiag_dst, 16);
            /* atributos: tcp_info */
            int alen = (int)(h->nlmsg_len - NLMSG_LENGTH(sizeof *m));
            for (struct rtattr *a = (struct rtattr *)(m + 1); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
                if (a->rta_type != INET_DIAG_INFO) continue;
                const struct tcp_info *ti = RTA_DATA(a);
                size_t need = offsetof(struct tcp_info, tcpi_bytes_received) + sizeof ti->tcpi_bytes_received;
                if (RTA_PAYLOAD(a) >= need) {
                    s->sent = ti->tcpi_bytes_acked;
                    s->recv = ti->tcpi_bytes_received;
                    s->bytes = 1;
                }
            }
            nsock++;
        }
    }
}

static int sock_dump(void) {
    nsock = 0;
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0) return -1;
    int ok = 0;
    const int fams[2] = { AF_INET, AF_INET6 }, protos[2] = { IPPROTO_TCP, IPPROTO_UDP };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) ok |= !dump(fd, fams[i], protos[j]);
    close(fd);
    return ok ? 0 : -1;
}

/* ---- inodo → proceso ------------------------------------------------------------------------ */

typedef struct { uint32_t inode; int pid; } Slot;
static Slot map[MAP_CAP];                       /* inodo → pid (pid -1: buscado y no encontrado) */
static double map_t;

static Slot *map_find(uint32_t ino, int add) {
    uint32_t h = (ino * 2654435761u) & (MAP_CAP - 1);
    for (int i = 0; i < MAP_CAP; i++, h = (h + 1) & (MAP_CAP - 1)) {
        if (map[h].inode == ino) return &map[h];
        if (!map[h].inode) {
            if (!add) return NULL;
            map[h].inode = ino;
            map[h].pid = -1;
            return &map[h];
        }
    }
    return NULL;
}

void conn_reset(void) { memset(map, 0, sizeof map); map_t = 0; }

/* Recorre los fd de los procesos del usuario y rehace el mapa. */
static void map_rebuild(void) {
    memset(map, 0, sizeof map);
    unsigned me = (unsigned)getuid();
    DIR *d = opendir("/proc");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        char p[64];
        struct stat st;
        snprintf(p, sizeof p, "/proc/%d", pid);
        if (stat(p, &st) || st.st_uid != me) continue;
        snprintf(p, sizeof p, "/proc/%d/fd", pid);
        DIR *fdd = opendir(p);
        if (!fdd) continue;
        struct dirent *f;
        while ((f = readdir(fdd))) {
            if (!isdigit((unsigned char)f->d_name[0])) continue;
            char lp[64], target[64];
            snprintf(lp, sizeof lp, "/proc/%d/fd/%d", pid, atoi(f->d_name));
            ssize_t k = readlink(lp, target, sizeof target - 1);
            if (k < 10 || strncmp(target, "socket:[", 8)) continue;
            target[k] = 0;
            uint32_t ino = (uint32_t)strtoul(target + 8, NULL, 10);
            Slot *s = ino ? map_find(ino, 1) : NULL;
            if (s && s->pid < 0) s->pid = pid;      /* el primero (los hijos heredan fds) */
        }
        closedir(fdd);
    }
    closedir(d);
    map_t = mon_now_ms();
}

/* ---- direcciones --------------------------------------------------------------------------- */

static int is_mapped4(const uint8_t *a) {
    static const uint8_t pre[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    return !memcmp(a, pre, 12);
}

/* "internet" | "lan" | "local" */
static const char *scope(int fam, const uint8_t *a) {
    if (fam == 4) {
        if (a[0] == 127 || (a[0] == 0)) return "local";
        if (a[0] == 10 || (a[0] == 172 && (a[1] & 0xf0) == 16) || (a[0] == 192 && a[1] == 168) ||
            (a[0] == 169 && a[1] == 254) || (a[0] == 100 && (a[1] & 0xc0) == 64) || a[0] >= 224) return "lan";
        return "internet";
    }
    static const uint8_t zero[16], one[16] = { [15] = 1 };
    if (!memcmp(a, zero, 16) || !memcmp(a, one, 16)) return "local";
    if ((a[0] == 0xfe && (a[1] & 0xc0) == 0x80) || (a[0] & 0xfe) == 0xfc || a[0] == 0xff) return "lan";
    return "internet";
}

static const char *const TCP_STATE[] = { "", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2", "TIME_WAIT",
                                         "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING", "NEW_SYN_RECV" };

/* Nombre y ejecutable de un pid (caché chica por muestra). */
static struct { int pid; char name[64], exe[PATH_MAX]; } pinfo[256];
static int npinfo;

static int proc_info(int pid) {
    for (int i = 0; i < npinfo; i++) if (pinfo[i].pid == pid) return i;
    if (npinfo >= 256) return -1;
    int i = npinfo++;
    pinfo[i].pid = pid;
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/comm", pid);
    if (mon_read_small(p, pinfo[i].name, sizeof pinfo[i].name) <= 0) pinfo[i].name[0] = 0;
    pinfo[i].name[strcspn(pinfo[i].name, "\n")] = 0;
    snprintf(p, sizeof p, "/proc/%d/exe", pid);
    ssize_t k = readlink(p, pinfo[i].exe, sizeof pinfo[i].exe - 1);
    pinfo[i].exe[k > 0 ? k : 0] = 0;
    return i;
}

void conn_scan(Buf *b, const Mmdb *geo) {
    unsigned me = (unsigned)getuid();
    if (sock_dump()) { mon_buf_printf(b, "{\"t\":%.0f,\"ok\":false,\"others\":0,\"list\":[]}", mon_wall_ms()); return; }
    /* puertos TCP en escucha (de cualquiera): una conexión a uno de ellos es entrante */
    static uint8_t listening_port[8192];
    memset(listening_port, 0, sizeof listening_port);
    int unknown = 0, others = 0;
    for (int i = 0; i < nsock; i++) {
        Sock *s = &socks[i];
        if (s->proto == IPPROTO_TCP && s->state == 10) listening_port[s->sport >> 3] |= (uint8_t)(1 << (s->sport & 7));
        if (s->uid != me) { others++; continue; }
        if (!s->inode) continue;
        Slot *sl = map_find(s->inode, 0);
        if (!sl) unknown = 1;
    }
    /* rehacer el mapa solo si apareció un socket nuevo (y como mucho una vez por segundo) */
    if (unknown && mon_now_ms() - map_t > 900) {
        map_rebuild();
        for (int i = 0; i < nsock; i++)                     /* los que siguen sin dueño: -1 */
            if (socks[i].uid == me && socks[i].inode) map_find(socks[i].inode, 1);
    }
    npinfo = 0;
    int self = (int)getpid(), n = 0;
    mon_buf_printf(b, "{\"t\":%.0f,\"ok\":true,\"list\":[", mon_wall_ms());
    for (int i = 0; i < nsock; i++) {
        Sock *s = &socks[i];
        if (s->uid != me || !s->inode) continue;
        int fam = s->fam;
        uint8_t src[16], dst[16];
        memcpy(src, s->src, 16);
        memcpy(dst, s->dst, 16);
        static const uint8_t any[16];
        if (fam == 6 && is_mapped4(src) && (is_mapped4(dst) || !memcmp(dst, any, 16))) {   /* ::ffff:a.b.c.d */
            fam = 4;
            memmove(src, src + 12, 4);
            memmove(dst, dst + 12, 4);
        }
        char ls[INET6_ADDRSTRLEN], rs[INET6_ADDRSTRLEN];
        inet_ntop(fam == 4 ? AF_INET : AF_INET6, src, ls, sizeof ls);
        inet_ntop(fam == 4 ? AF_INET : AF_INET6, dst, rs, sizeof rs);
        int tcp = s->proto == IPPROTO_TCP;
        const char *dir;
        if (tcp && s->state == 10) dir = "listen";
        else if (!tcp && !s->dport) dir = "listen";             /* UDP sin destino: escucha */
        else if (tcp && (listening_port[s->sport >> 3] & (1 << (s->sport & 7)))) dir = "in";
        else dir = "out";
        int listening = !strcmp(dir, "listen");
        const char *sc = listening ? "local" : scope(fam, dst);
        char cc[3] = "";
        if (geo && !strcmp(sc, "internet")) mmdb_country(geo, dst, fam, cc);
        Slot *sl = map_find(s->inode, 0);
        int pid = sl ? sl->pid : -1, pi = pid > 0 ? proc_info(pid) : -1;
        mon_buf_printf(b, "%s{\"id\":%u,\"proto\":\"%s\",\"fam\":%d,\"state\":\"%s\",\"dir\":\"%s\",\"local\":\"%s\",\"lport\":%u,"
                      "\"remote\":\"%s\",\"rport\":%u,\"scope\":\"%s\",\"cc\":\"%s\",\"pid\":%d,\"self\":%s,\"name\":",
                   n ? "," : "", s->inode, tcp ? "tcp" : "udp", fam, tcp && s->state < 13 ? TCP_STATE[s->state] : (s->dport ? "CONNECTED" : "UNCONN"),
                   dir, ls, s->sport, listening ? "" : rs, s->dport, sc, cc, pid, pid == self ? "true" : "false");
        mon_buf_str(b, pi >= 0 ? pinfo[pi].name : "");
        mon_buf_printf(b, ",\"exe\":");
        mon_buf_str(b, pi >= 0 ? pinfo[pi].exe : "");
        if (s->bytes) mon_buf_printf(b, ",\"sent\":%llu,\"recv\":%llu}", (unsigned long long)s->sent, (unsigned long long)s->recv);
        else mon_buf_printf(b, ",\"sent\":null,\"recv\":null}");
        n++;
    }
    mon_buf_printf(b, "],\"others\":%d}", others);
}
