/* bpfsys.c — syscall bpf() directo (ver bpfsys.h). */
#include "netperm/bpfsys.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int sys_bpf(int cmd, union bpf_attr *a) {
    return (int)syscall(__NR_bpf, cmd, a, sizeof *a);
}

static void set_name(char *dst, const char *name) {
    memset(dst, 0, BPF_OBJ_NAME_LEN);
    if (!name) return;
    for (size_t i = 0; name[i] && i < BPF_OBJ_NAME_LEN - 1; i++)
        dst[i] = (name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ? name[i] : '_';
}

int bpfs_map_create(uint32_t type, uint32_t key, uint32_t val, uint32_t max, uint32_t flags, const char *name) {
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.map_type = type;
    a.key_size = key;
    a.value_size = val;
    a.max_entries = max;
    a.map_flags = flags;
    set_name(a.map_name, name);
    int fd = sys_bpf(BPF_MAP_CREATE, &a);
    if (fd < 0 && errno == EINVAL) {                /* kernels sin nombres de mapa */
        memset(a.map_name, 0, sizeof a.map_name);
        fd = sys_bpf(BPF_MAP_CREATE, &a);
    }
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int bpfs_map_update(int fd, const void *key, const void *val, uint64_t flags) {
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.map_fd = (uint32_t)fd;
    a.key = (uint64_t)(uintptr_t)key;
    a.value = (uint64_t)(uintptr_t)val;
    a.flags = flags;
    return sys_bpf(BPF_MAP_UPDATE_ELEM, &a);
}

int bpfs_map_lookup(int fd, const void *key, void *val) {
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.map_fd = (uint32_t)fd;
    a.key = (uint64_t)(uintptr_t)key;
    a.value = (uint64_t)(uintptr_t)val;
    return sys_bpf(BPF_MAP_LOOKUP_ELEM, &a);
}

int bpfs_map_delete(int fd, const void *key) {
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.map_fd = (uint32_t)fd;
    a.key = (uint64_t)(uintptr_t)key;
    return sys_bpf(BPF_MAP_DELETE_ELEM, &a);
}

int bpfs_prog_load(uint32_t type, uint32_t attach, const NpProgDesc *d, int (*map_fd)(const char *), char *log, size_t loglen) {
    if (!d || !d->ninsns || d->ninsns > 1000000) { errno = EINVAL; return -1; }
    struct bpf_insn *ins = malloc(d->ninsns * sizeof *ins);
    if (!ins) return -1;
    memcpy(ins, d->insns, d->ninsns * sizeof *ins);
    for (uint32_t i = 0; i < d->nrelocs; i++) {
        uint32_t k = d->relocs[i].insn;
        int fd = map_fd(d->relocs[i].map);
        if (k + 1 >= d->ninsns || ins[k].code != (BPF_LD | BPF_IMM | BPF_DW) || fd < 0) { free(ins); errno = EINVAL; return -1; }
        ins[k].src_reg = BPF_PSEUDO_MAP_FD;
        ins[k].imm = fd;
        ins[k + 1].imm = 0;
    }
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.prog_type = type;
    a.expected_attach_type = attach;
    a.insns = (uint64_t)(uintptr_t)ins;
    a.insn_cnt = d->ninsns;
    a.license = (uint64_t)(uintptr_t)"LordOS";
    set_name(a.prog_name, d->name);
    int fd = sys_bpf(BPF_PROG_LOAD, &a);
    if (fd < 0 && log && loglen > 1) {
        int e = errno;
        memset(log, 0, loglen);
        a.log_buf = (uint64_t)(uintptr_t)log;
        a.log_size = (uint32_t)(loglen > (16u << 20) ? (16u << 20) : loglen);
        a.log_level = 1;
        fd = sys_bpf(BPF_PROG_LOAD, &a);
        if (fd < 0) errno = e;
    }
    free(ins);
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int bpfs_link_create(int prog_fd, int target_fd, uint32_t attach) {
    union bpf_attr a;
    memset(&a, 0, sizeof a);
    a.link_create.prog_fd = (uint32_t)prog_fd;
    a.link_create.target_fd = (uint32_t)target_fd;
    a.link_create.attach_type = attach;
    int fd = sys_bpf(BPF_LINK_CREATE, &a);
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int bpfs_ring_open(BpfRing *r, int map_fd, size_t size) {
    memset(r, 0, sizeof *r);
    r->fd = map_fd;
    r->size = size;
    r->page = (size_t)sysconf(_SC_PAGESIZE);
    r->cons = mmap(NULL, r->page, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
    if (r->cons == MAP_FAILED) { r->cons = NULL; return -1; }
    r->prod = mmap(NULL, r->page + 2 * size, PROT_READ, MAP_SHARED, map_fd, (off_t)r->page);
    if (r->prod == MAP_FAILED) { munmap(r->cons, r->page); r->cons = r->prod = NULL; return -1; }
    return 0;
}

int bpfs_ring_drain(BpfRing *r, void (*cb)(void *u, const void *data, uint32_t len), void *u) {
    if (!r->cons) return 0;
    unsigned long *consp = r->cons, *prodp = r->prod;
    const uint8_t *data = (const uint8_t *)r->prod + r->page;
    unsigned long cons = __atomic_load_n(consp, __ATOMIC_ACQUIRE);
    int n = 0;
    for (;;) {
        unsigned long prod = __atomic_load_n(prodp, __ATOMIC_ACQUIRE);
        if (cons >= prod) break;
        const uint32_t *hdr = (const uint32_t *)(data + (cons & (r->size - 1)));
        uint32_t len = __atomic_load_n(hdr, __ATOMIC_ACQUIRE);
        if (len & BPF_RINGBUF_BUSY_BIT) break;
        uint32_t body = len & ~(uint32_t)(BPF_RINGBUF_BUSY_BIT | BPF_RINGBUF_DISCARD_BIT);
        if (!(len & BPF_RINGBUF_DISCARD_BIT) && body <= r->size) { cb(u, hdr + 2, body); n++; }
        cons += (body + BPF_RINGBUF_HDR_SZ + 7) & ~7ul;
        __atomic_store_n(consp, cons, __ATOMIC_RELEASE);
    }
    return n;
}

void bpfs_ring_close(BpfRing *r) {
    if (r->cons) munmap(r->cons, r->page);
    if (r->prod) munmap(r->prod, r->page + 2 * r->size);
    r->cons = r->prod = NULL;
}

uint64_t bpfs_cgroup_id(int dirfd, const char *name) {
    _Alignas(8) unsigned char buf[sizeof(struct file_handle) + sizeof(uint64_t)];
    struct file_handle *fh = (struct file_handle *)(void *)buf;
    int mount_id;
    memset(buf, 0, sizeof buf);
    fh->handle_bytes = sizeof(uint64_t);
    if (name_to_handle_at(dirfd, name, fh, &mount_id, 0) == 0 && fh->handle_bytes == sizeof(uint64_t)) {
        uint64_t id;
        memcpy(&id, fh->f_handle, sizeof id);
        return id;
    }
    struct stat st;                                  /* respaldo: en 64 bits el id es el inodo */
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode)) return (uint64_t)st.st_ino;
    return 0;
}
