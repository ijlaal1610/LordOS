// LordBench's reference machines (docs/BENCHMARK.md, "Results without one magic number"): a result is
// shown as "×N" of each one measured with the same LordBench version. Every entry is a real Full run,
// copied from the lordbench.json it wrote, with the machine described in sysinfo.perf.ref.<id>.
// Planned: a Raspberry Pi 5 8 GB and a modest x86 PC; they are added when they have been measured.
export const REFERENCES = [
  {
    // VM 116 on Proxmox VE: 8 vCPU (host CPU, Xeon Silver 4108 at 1.8 GHz), 4 GB, virtio disk, no
    // GPU passthrough (Mesa llvmpipe); LordOS 0.8.5 + LordBench 1, 2026-10-27, Full with 30 s sustained
    id: 'vm', lordbench: 1, mode: 'full',
    results: {
      'cpu.sha256.1': { value: 166.557 }, 'cpu.sha256.n': { value: 976.519 },
      'cpu.matmul.1': { value: 3.41756 }, 'cpu.matmul.n': { value: 19.6958 },
      'cpu.xz.1': { value: 60.9097 }, 'cpu.xz.n': { value: 353.612 },
      'cpu.js.1': { value: 58.2072 }, 'cpu.js.n': { value: 335.649 },
      'mem.copy': { value: 20.157 }, 'mem.triad': { value: 22.7711 }, 'mem.latency': { value: 90.1192 },
      'gpu.scene': { value: 793.718 }, 'gpu.fill': { value: 394.575 }, 'gpu.upload': { value: 9676.24 }, 'gpu.layers': { value: 22.6118 },
      'disk.seqwrite': { value: 219.028 }, 'disk.seqread': { value: 517.082 }, 'disk.random4k': { value: 5619.17 },
      'sustained.start': { value: 19.7626 }, 'sustained.end': { value: 19.7667 },
    },
  },
];
