/* bpfsys.h — lo mínimo del syscall bpf() para lord-netperm: mapas, cargar programas, engancharlos
 * a una cgroup con un bpf_link y leer un anillo (BPF_MAP_TYPE_RINGBUF). Sin libbpf ni bcc.
 *
 * Los programas vienen ya compilados dentro del binario (build/gen/np_bpf.h, que arma bpf2c desde
 * el .o de clang): nunca se lee un programa de un archivo en tiempo de ejecución. */
#ifndef LORD_NP_BPFSYS_H
#define LORD_NP_BPFSYS_H

#include <stddef.h>
#include <stdint.h>

/* Lo que genera build/bpf2c (ver np_bpf.h). */
typedef struct {
    uint32_t insn;              /* instrucción ld_imm64 que lleva el fd del mapa */
    const char *map;            /* nombre del mapa (np_apps…) */
} NpReloc;

typedef struct {
    const char *section;        /* "cgroup/connect4"… */
    const char *name;           /* nombre de la función */
    const uint64_t *insns;
    uint32_t ninsns;
    const NpReloc *relocs;
    uint32_t nrelocs;
} NpProgDesc;

int  bpfs_map_create(uint32_t type, uint32_t key, uint32_t val, uint32_t max, uint32_t flags, const char *name);
int  bpfs_map_update(int fd, const void *key, const void *val, uint64_t flags);
int  bpfs_map_lookup(int fd, const void *key, void *val);
int  bpfs_map_delete(int fd, const void *key);

/* Carga un programa con las reubicaciones resueltas (map_fd(nombre) da el fd, -1 si no existe).
 * log (opcional) recibe el registro del verificador si falla. Devuelve el fd o -1. */
int  bpfs_prog_load(uint32_t type, uint32_t attach, const NpProgDesc *d, int (*map_fd)(const char *), char *log, size_t loglen);
int  bpfs_link_create(int prog_fd, int target_fd, uint32_t attach);

/* Anillo: mmap de la página del consumidor (rw) y del productor + datos (ro). */
typedef struct {
    int fd;
    size_t size, page;
    void *cons, *prod;
} BpfRing;

int  bpfs_ring_open(BpfRing *r, int map_fd, size_t size);
/* Llama a cb por cada registro listo; devuelve cuántos leyó. */
int  bpfs_ring_drain(BpfRing *r, void (*cb)(void *u, const void *data, uint32_t len), void *u);
void bpfs_ring_close(BpfRing *r);

/* Id de cgroup v2 de una carpeta (el que da bpf_get_current_cgroup_id), o 0. */
uint64_t bpfs_cgroup_id(int dirfd, const char *name);

#endif
