# Versioned results

Measurements and checks are published as files, per LordOS version and per machine, with the commands
that produced them, so anyone can repeat them and compare. A screenshot proves one moment; these files
can be checked.

```
results/<version>/<machine>/
    README.md        the conditions (hardware or VM, monitor or headless, idle time, apps open, power)
                     and the exact commands
    sysreport.json   lordos-sysreport report --json: the hardware and what LordOS made of it, with no
                     serial numbers, addresses or user and host names (tests/sysreport_test.c checks it)
    testbench.json   lordos-testbench --gl --auto N: the Wayland checklist, frame times (average, p50,
                     p99, frames that missed the refresh budget), presentation, the 3D renderer, latency
    memory.txt       the whole system (`free -m`) and PSS per process (/proc/<pid>/smaps_rollup)
    boot.txt         systemd-analyze
    lordbench.json   from 0.8.6, LordBench (docs/BENCHMARK.md)
```

`tools/results/collect.sh <machine> [--no-memory] [--bench-seconds N] [--version V]` writes such a
folder; run it as the user inside a LordOS session, and with `sudo` to include root's processes in the PSS
list. Fill in the conditions in its README before committing. Each release attaches its `results/<version>`
folder to the GitHub release (`docs/RELEASING.md`).

## Memory: two magnitudes

`free`'s "used" is the whole system's memory (kernel, Debian's services, LordOS's services, the
session), without reclaimable page cache: this is the number to compare between systems, on the same
machine, after the same idle time, with the same apps open. A process's RSS (`ps`, `/proc/<pid>/status`)
counts every shared library page it maps, so RSS figures of several processes overlap and can't be added;
PSS divides each shared page among the processes that map it, so PSS figures add up. The paper's §7.1 keeps
the two apart.

## Results so far

| Version | Machine | Conditions | What |
|---|---|---|---|
| 0.8.5~rc1 | `vmware-fusion` | VMware Fusion VM, virtual GPU (`vmwgfx`, SVGA3D), 1280×800 @ 60 Hz, updated from 0.8.4.1 by `apt install` | whole system 606–622 MB used one minute after sign-in, PSS of all processes 316 MB (login screen 78 MB of it); 59.8 fps at 60 Hz, p99 18.4 ms; 17 Wayland checks passed; boot 7.5 s |
| 0.8.5~rc1 | `vmware-fusion-before-pacing` | the same, before software vblank pacing | 524 fps at 60 Hz: the driver completes flips at once |
| 0.8.5-dev | `vm116-headless` | Proxmox VM, no GPU, headless session (remote desktop only), development install | sysreport, testbench (16 Wayland checks passed; `llvmpipe`; no frame presented without a monitor), boot |
