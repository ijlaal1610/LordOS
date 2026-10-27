/* lbproto.c — see lbproto.h. */
#include "lbproto.h"

#include <math.h>
#include <stddef.h>

static const struct { const char *name, *unit; } meas[LB_COUNT] = {
    [LB_CPU_SHA1] = { "cpu.sha256.1", "MB/s" },     [LB_CPU_SHAN] = { "cpu.sha256.n", "MB/s" },
    [LB_CPU_MAT1] = { "cpu.matmul.1", "GFLOP/s" },  [LB_CPU_MATN] = { "cpu.matmul.n", "GFLOP/s" },
    [LB_CPU_XZ1] = { "cpu.xz.1", "MB/s" },          [LB_CPU_XZN] = { "cpu.xz.n", "MB/s" },
    [LB_CPU_JS1] = { "cpu.js.1", "runs/s" },        [LB_CPU_JSN] = { "cpu.js.n", "runs/s" },
    [LB_MEM_COPY] = { "mem.copy", "GB/s" },         [LB_MEM_TRIAD] = { "mem.triad", "GB/s" },
    [LB_MEM_LAT] = { "mem.latency", "ns" },
    [LB_GPU_SCENE] = { "gpu.scene", "fps" },        [LB_GPU_FILL] = { "gpu.fill", "Mpx/s" },
    [LB_GPU_UPLOAD] = { "gpu.upload", "MB/s" },     [LB_GPU_LAYERS] = { "gpu.layers", "fps" },
    [LB_DISK_SEQW] = { "disk.seqwrite", "MB/s" },   [LB_DISK_SEQR] = { "disk.seqread", "MB/s" },
    [LB_DISK_RAND] = { "disk.random4k", "IOPS" },
    [LB_SUS_START] = { "sustained.start", "GFLOP/s" }, [LB_SUS_END] = { "sustained.end", "GFLOP/s" },
};

const char *lb_name(int id) { return id >= 0 && id < LB_COUNT ? meas[id].name : NULL; }
const char *lb_unit(int id) { return id >= 0 && id < LB_COUNT ? meas[id].unit : NULL; }
int lb_lower_better(int id) { return id == LB_MEM_LAT; }

int lb_msg_valid(const LbMsg *m) {
    if (m->magic != LB_MAGIC || !isfinite(m->value) || !isfinite(m->aux)) return 0;
    switch (m->kind) {
    case LB_MSG_STEP: return m->id < LB_COUNT;
    case LB_MSG_RESULT: return m->id < LB_COUNT && m->value > 0 && m->value < 1e12 && m->aux >= 0 && m->aux < 1e12;
    case LB_MSG_SKIP: return m->id < LB_COUNT && m->aux >= LB_SKIP_NODEV && m->aux <= LB_SKIP_MEMORY;
    case LB_MSG_SAMPLE: return m->value >= 0 && m->value < 1e9 && m->aux >= 0 && m->aux < 86400;
    case LB_MSG_DONE: return m->aux >= 0 && m->aux < 65536;
    case LB_MSG_GPUINFO: return m->value == 0 || m->value == 1 || m->value == 2;
    }
    return 0;
}
