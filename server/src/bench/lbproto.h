/* lbproto.h — the channel between lord-server (lbrun.c) and lord-bench (lbmain.c), LordBench's
 * sandboxed helper (docs/BENCHMARK.md, "0.8.6 — LordBench").
 *
 * A SOCK_SEQPACKET pair on fd 3. lord-server sends one LbGo (with up to two fds: the GPU's render node
 * and a storage scratch file, both opened by lord-server); lord-bench answers only with fixed-size
 * LbMsg records: no text, no paths. lord-server checks every field (lb_msg_valid) and builds the JSON
 * itself. To stop a run, lord-server kills the helper. */
#ifndef LORD_BENCH_LBPROTO_H
#define LORD_BENCH_LBPROTO_H

#include <stdint.h>

#define LB_MAGIC   0x4c424e31u          /* "LBN1" */
#define LB_VERSION 1                    /* LordBench's version: results compare only with the same one */

/* what to run */
enum { LB_MODE_QUICK = 1, LB_MODE_FULL = 2 };
/* LbGo.flags */
enum {
    LB_GO_GPU     = 1,                  /* an fd with the render node follows */
    LB_GO_STORAGE = 2,                  /* an fd with the scratch file follows (after the GPU's) */
    LB_GO_LOW     = 4,                  /* desktop impact: lowest priority */
};

typedef struct {
    uint32_t magic, mode, flags, threads;
    uint32_t storage_mb;                /* the scratch file's size limit */
    uint32_t sustained_s;               /* the sustained test's length (Full only), 0 = none */
} LbGo;

/* the measurements; their names and units are lb_name()/lb_unit() */
enum {
    LB_CPU_SHA1 = 0,                    /* SHA-256, one core, MB/s */
    LB_CPU_SHAN,                        /* SHA-256, all cores, MB/s */
    LB_CPU_MAT1,                        /* matrix multiply, one core, GFLOP/s */
    LB_CPU_MATN,                        /* matrix multiply, all cores, GFLOP/s */
    LB_CPU_XZ1,                         /* xz decompression (the package engine's decoder), one core, MB/s */
    LB_CPU_XZN,                         /* the same on all cores, MB/s */
    LB_CPU_JS1,                         /* QuickJS running a fixed script, one core, runs/s */
    LB_CPU_JSN,                         /* the same on all cores, runs/s */
    LB_MEM_COPY,                        /* memory bandwidth, copy, GB/s */
    LB_MEM_TRIAD,                       /* memory bandwidth, triad, GB/s */
    LB_MEM_LAT,                         /* memory latency, a pointer chase over 64 MB, ns */
    LB_GPU_SCENE,                       /* the 3D scene offscreen at 1920×1080, frames/s */
    LB_GPU_FILL,                        /* fill rate, Mpixel/s */
    LB_GPU_UPLOAD,                      /* texture upload, MB/s */
    LB_GPU_LAYERS,                      /* composing 8 screen-size layers, frames/s */
    LB_DISK_SEQW,                       /* storage, sequential write, MB/s */
    LB_DISK_SEQR,                       /* storage, sequential read, MB/s */
    LB_DISK_RAND,                       /* storage, random 4 KiB reads, IOPS */
    LB_SUS_START,                       /* sustained: all-core matrix multiply, first 10 s, GFLOP/s */
    LB_SUS_END,                         /* sustained: last 10 s, GFLOP/s */
    LB_COUNT
};

/* LbMsg.kind */
enum {
    LB_MSG_STEP = 1,                    /* id: the measurement starting now */
    LB_MSG_RESULT,                      /* id, value (and aux: the spread, max − min over the repeats) */
    LB_MSG_SKIP,                        /* id, why (aux, an LB_SKIP_*) */
    LB_MSG_SAMPLE,                      /* sustained: value = GFLOP/s of the last interval, aux = seconds since start */
    LB_MSG_DONE,                        /* the run ended; aux = the sandbox mask */
    LB_MSG_GPUINFO,                     /* value = 0 hardware, 1 virtual, 2 software (bench_gl_kind) */
};
enum { LB_SKIP_NODEV = 1, LB_SKIP_FAILED, LB_SKIP_MODE, LB_SKIP_MEMORY };

typedef struct {
    uint32_t magic, kind, id, pad;
    double value, aux;
} LbMsg;

const char *lb_name(int id);            /* "cpu.sha256.1", …; NULL out of range */
const char *lb_unit(int id);            /* "MB/s", "GFLOP/s", "runs/s", "GB/s", "ns", "fps", "Mpx/s", "IOPS" */
int         lb_lower_better(int id);    /* 1 for latency */
/* 1 if a record from the helper is acceptable: magic, kind, id in range, finite values in range */
int         lb_msg_valid(const LbMsg *m);

#endif
