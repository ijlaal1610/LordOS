/* bpfsys.h — the bare minimum of the bpf() syscall for lord-netperm: maps, loading programs,
 * attaching them to a cgroup with a bpf_link and reading a ring buffer (BPF_MAP_TYPE_RINGBUF).
 * No libbpf or bcc.
 *
 * The programs come precompiled inside the binary (build/gen/np_bpf.h, which bpf2c builds from
 * clang's .o): a program is never read from a file at run time. */
#ifndef LORD_NP_BPFSYS_H
#define LORD_NP_BPFSYS_H

#include <stddef.h>
#include <stdint.h>

/* What build/bpf2c generates (see np_bpf.h). */
typedef struct {
    uint32_t insn;              /* ld_imm64 instruction that carries the map fd */
    const char *map;            /* map name (np_apps…) */
} NpReloc;

typedef struct {
    const char *section;        /* "cgroup/connect4"… */
    const char *name;           /* function name */
    const uint64_t *insns;
    uint32_t ninsns;
    const NpReloc *relocs;
    uint32_t nrelocs;
} NpProgDesc;

int  bpfs_map_create(uint32_t type, uint32_t key, uint32_t val, uint32_t max, uint32_t flags, const char *name);
int  bpfs_map_update(int fd, const void *key, const void *val, uint64_t flags);
int  bpfs_map_lookup(int fd, const void *key, void *val);
int  bpfs_map_delete(int fd, const void *key);

/* Loads a program with its relocations resolved (map_fd(name) gives the fd, -1 if it doesn't
 * exist). log (optional) receives the verifier log if it fails. Returns the fd or -1. */
int  bpfs_prog_load(uint32_t type, uint32_t attach, const NpProgDesc *d, int (*map_fd)(const char *), char *log, size_t loglen);
int  bpfs_link_create(int prog_fd, int target_fd, uint32_t attach);

/* Ring buffer: mmap of the consumer page (rw) and of the producer + data (ro). */
typedef struct {
    int fd;
    size_t size, page;
    void *cons, *prod;
} BpfRing;

int  bpfs_ring_open(BpfRing *r, int map_fd, size_t size);
/* Calls cb for each ready record; returns how many it read. */
int  bpfs_ring_drain(BpfRing *r, void (*cb)(void *u, const void *data, uint32_t len), void *u);
void bpfs_ring_close(BpfRing *r);

/* cgroup v2 id of a folder (the one bpf_get_current_cgroup_id gives), or 0. */
uint64_t bpfs_cgroup_id(int dirfd, const char *name);

#endif
