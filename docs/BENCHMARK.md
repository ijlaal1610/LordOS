# System Information, LordBench and hardware reports (proposal)

**Status: design proposal; the 0.8.5 collector and `lordos-sysreport` are built (below), the rest is not.** It plans three releases and a later stage:

| Release | What |
|---|---|
| **0.8.5 — System Information** | Every piece of hardware detected, what LordOS uses (above all the GPU), the services, the paper's claims checked on this machine, JSON/Markdown reports. |
| **0.8.6 — LordBench** | Measurements of CPU, memory, GPU, the compositor, video and storage under recorded, reproducible conditions, with versioned results. |
| **0.8.7 — Community hardware reports** | Publishing reports as GitHub issues, validating them, and a tested-hardware catalogue on lordos.org that the app also reads. |
| **Later — Lumo and AutoTune** | `ROADMAP.md`, "Lumo, the local assistant": explains a report and tunes settings from what the machine measured. |

The aim is that every claim LordOS makes (memory at idle, GPU use, isolation) can be backed by a
reproducible report, not only a screenshot. Results must be technically defensible first and
attractive second.

## Architecture: the logic is not in the interface

The collector, the checks and the benchmark are native components with no UI, used by three
clients so no logic is duplicated:

```
server/src/sysreport/        collector + checks (library, read-only, no root)
server/src/bench/            LordBench workloads (library)
        │
        ├── lord-server: lord.sysinfo (js_hw.c) → apps/sysinfo        (the app)
        ├── lordos-sysreport  report | checks | bench [--quick|--full] --json  (CLI, runs as the user)
        └── tests (fake sysfs trees, fixtures) and, later, Lumo's tools
```

- **`lordos-sysreport`** is an ordinary user command in `/usr/bin` that the user or a test script
  runs; `lord-server` never runs it. It writes the same JSON as the app, so reports can be made on
  a machine without a session (a headless Pi, a VM over SSH, CI).
- **`lord-bench`** is the helper the app starts for workloads, launched like `lord-doc` and
  `lord-media`: fixed path (`<lord-server dir>/lord-bench`), no arguments, minimal environment,
  channel on fd 3, `sandbox_apply_helper()` before any work; it gets only the render node and a
  temporary file as fds. It links libc and Mesa's EGL/GLES (already used by the GPU path), nothing
  new. The CLI links the same library directly.

## 0.8.5 — System Information

One native app, **System Information** (`apps/sysinfo`), also opened from About LordOS →
**System Report…**. Tabs: **Overview**, **Hardware**, **Compatibility**, **Checks**, **Report**
(LordBench adds **Performance** in 0.8.6).

### Hardware

A sidebar tree and a detail pane, with vendor and device names from `pci.ids` and `usb.ids`:
hardware overview (model, vendor, firmware version and date, board, chassis type, Secure Boot),
CPU (cores, threads, frequencies, caches, SSE4.2/AVX/AVX2/AVX-512/FMA, NEON/SVE, governor,
microcode), memory (total, swap, zram; per module through the read-only admin action
`hw-memory-modules`), graphics, displays (monitor name and size from the EDID, mode, refresh,
scale, card, scanout), storage (interface and link speed, size, SMART health), USB and
Thunderbolt, PCI with drivers and missing firmware, audio, network, Bluetooth, power, thermal and
fans, cameras and input, Raspberry Pi (revision, bootloader, throttling and under-voltage flags,
fan header, kernel revision), software (Debian, kernel, the kernel options LordOS depends on, the
LordOS services and their state, the code manifest).

Serials, UUIDs, MAC addresses and EDID serials are shown only on screen, on request, and never
enter a report.

### The GPU as separate layers

"Is the GPU used" is not one answer. For every card the app shows each layer on its own, with
**Available**, **Not available**, **Not checked** or **Not measurable on this driver**:

| Layer | How it is known |
|---|---|
| Device detected | PCI or platform device, vendor and model |
| Kernel driver loaded | the device's `driver` link (`amdgpu`, `i915`, `xe`, `nouveau`, `nvidia-drm`, `v3d`/`vc4`, `virtio_gpu`…) |
| DRM/KMS available | a card node with connectors, modesetting working (`kms.c`) |
| Render node | `/dev/dri/renderD*` openable by the session |
| EGL context active | `lord-server` created one; the GL renderer string; hardware or software (llvmpipe) |
| Composition | the GPU path is on, or the exact reason it's off (the same one the log gives) |
| Direct scanout | seen during a fullscreen video or app, with the count since the session started |
| Video decode | VA-API profiles per codec (`MK_HWPROBE`); on the Pi 5 the HEVC V4L2 decoder |
| Video encode | the remote desktop's encoder when it's used |
| Vulkan | ICD files present only ("Not checked"); loading Vulkan belongs to stage 2 |

Live load is shown only where the driver exposes it (`amdgpu`: busy % and VRAM); elsewhere the app
says it isn't measurable instead of showing a zero. The Nvidia proprietary driver is shown as
**untested** with an invitation to send a report.

### Checks: configured versus effective

Each check says what it verified, at one of two levels, and never claims the stronger one without
the test:

- **Configured**: the mechanism is present (a kernel option, a service running, a flag in
  `/proc`).
- **Effective**: a probe tried something that must fail, and it failed.

| Id | Paper | Configured | Effective |
|---|---|---|---|
| `sec.server-user` | §2.2 | `lord-server`'s uid ≠ 0 | — |
| `sec.app-sandbox` | §6.4 | `Seccomp: 2`, `NoNewPrivs: 1` on an isolated app | a probe app (a system package started like any isolated app) tries to read a file outside its grant, open a socket, `ptrace`, create a user namespace and reach another app's scope; every attempt must be refused |
| `sec.helper-sandbox` | §6.7 | — | `lord-bench` itself checks after its own lock-in that `open` of a path, `socket` and `execve` fail |
| `sec.landlock` | §6.4 | Landlock ABI ≥ 6 | covered by the probe app |
| `sec.netperm` | §6.6 | `lord-netperm` running, programs attached, devices and Bluetooth programs loaded | the probe app, denied the network by a temporary rule, fails to connect to a local listener |
| `sec.code` | §6.9 | manifest present | every loaded bundle verified in this session |
| `sec.remote-closed` | §11.4 | remote desktop setting | nothing listens on 5673 when it's off |
| `sec.cgroups` | §11.4 | root-owned session tree, one scope per app | — |
| `perf.idle-memory` | §7.1 | — | memory used after 60 s idle at the desktop, with the session's resolution and number of displays recorded |
| `perf.idle-cpu` | §7.1 | — | CPU idle over 30 s with nothing open |
| `perf.boot` | §7.1 | — | systemd's firmware, loader, kernel and userspace times |
| `gpu.used` | §7.3 | hardware renderer exists | GPU path on, or a warning with the reason |
| `sys.time`, `sys.firmware`, `sys.fans`, `sys.memcg`, `sys.pi-bootloader`, `sys.updates` | various | as listed in the Hardware tree | — |

Thresholds live in a signed data file (`assets/sysinfo/checks.json`, in the code manifest). A
failed check offers **Report a problem** with the bug template.

### Compatibility

A panel per component (GPU, display, storage, network, Wi-Fi, Bluetooth, audio, power, suspend)
with its state: **Works**, **Works with limits** (and which), **Doesn't work**, **Not checked**. In
0.8.5 it comes only from this machine's detection and checks; from 0.8.7 it also shows what other
users confirmed for the same device and LordOS version, marked as community data.

### Reports

A versioned JSON (`schema: 1`) and a Markdown summary, saved to `~/Documents`:

- **Built from a fixed allow-list of fields.** Serials, UUIDs, MAC and IP addresses, host and user
  names, Wi-Fi network names and home-folder paths are never collected into it; a unit test feeds a
  fake system full of such values and checks none reaches the output.
- **Preview first**: the exact text that would be shared, field by field.
- **Explicit consent** for every share, never remembered.

## 0.8.6 — LordBench

### Two kinds of tests, kept apart

- **Peak performance**: the workload gets the machine. LordBench first waits for a quiet system (CPU
  idle above a threshold for 10 s) and refuses or marks the result if something else keeps the CPU
  busy; it records the load it saw.
- **Desktop impact**: the same workloads at low priority while the compositor animates a scripted
  scene, measuring the compositor's frame times and input latency. This answers "does the desktop
  stay smooth under load", a separate question with separate results.

### Workloads

| Category | Workloads |
|---|---|
| CPU, one core and all cores | SHA-256 (`pkg/crypto.c`), deflate, a matrix multiply; LordOS's own work: layout and paint of a fixed scene, shaping a fixed paragraph in several scripts, QuickJS running a fixed script |
| Memory | bandwidth (copy, triad) and latency (pointer chase) |
| GPU | offscreen GLES on the render node: fill rate, texture upload, composing N screen-size layers, timed with fences |
| Compositor | `lord-server`'s own frame counters during a scripted window drag, CPU path and GPU path |
| Video | short test clips we generate (H.264, HEVC, VP9, AV1; 1080p and 4K) decoded in `lord-media`, hardware and software; the remote desktop's encoders |
| Storage | sequential and random I/O on a size-limited temporary file (Full only, with a warning on SD cards) |
| Sustained | several minutes of all-core load (Full only) |

### Frame times against the refresh budget

The compositor results are reported as the **real time to build a frame** (paint plus present),
and separately as the share of the display's budget it uses (16.7 ms at 60 Hz, 8.3 ms at 120 Hz,
6.9 ms at 144 Hz) and the frames missed. No fixed threshold for every display: a check passes when
the 99th percentile fits the budget of the refresh rate actually in use.

### Thermal and power safety

- The limits come from the hardware: the thermal zones' passive and critical trip points, the
  CPU's own throttle state (cpufreq dropping below the requested frequency), the Pi's throttling
  and under-voltage flags. A run stops before the passive trip point and when throttling starts in
  a peak test; in the sustained test throttling is the result, recorded with its time and
  temperature.
- No run on battery below 30 %; a laptop on battery is recorded as such.

### Recorded conditions

Each result carries what affects it: resolution, scale and refresh rate of every display, GPU path
on or off, power source and profile, governor, temperatures at start and end, throttling seen, the
system load before starting, kernel, Mesa and LordOS versions, LordBench version. Two results are
compared only when the LordBench version matches; differing conditions are shown next to the
comparison.

### Results without one magic number

Three separate verdicts, never folded into one score:

- **Performance**: per category, normalized to a set of **reference machines** (initially a
  Raspberry Pi 5 8 GB, a modest x86 PC and a VM), shown as "×N of reference" for each, so a board
  and a workstation are each compared with something similar.
- **Compatibility**: from the Compatibility panel.
- **Stability**: errors and crashes during the run, frames missed under load, throttling.

## 0.8.7 — Community hardware reports

- **Share on GitHub** saves the report as a small HTML page and opens it with the default browser
  (the existing `lord.apps.launchWith('', path)` rule). That page opens GitHub's new-issue form with
  the `hardware-report` template, title and summary filled in; the user attaches the JSON and
  submits it with their own account. LordOS sends nothing by itself and holds no token.
- A workflow validates each attached JSON against the schema, labels the issue by vendor and kind,
  and flags reports whose numbers are implausible for the hardware.
- **lordos.org/hardware**: the catalogue built from accepted reports: GPUs and drivers tested,
  boards, laptops with suspend, battery and Wi-Fi, known problems and the version that fixed them,
  real LordOS performance per device. The app's Compatibility panel reads a signed snapshot of it
  (downloaded with the package engine's checks), so it works offline.

## From the live USB, without installing

The app and LordBench are in the ISO, so anyone can start a PC from the LordOS USB stick, check
the machine and send a report without installing anything or touching its disks. This is likely
the main source of PC reports: it costs the tester nothing and answers "will LordOS work here"
before installing.

- **A boot menu entry**, *Check this computer*, starts the live session straight into System
  Information (0.8.5) and its Performance tab (0.8.6). The normal live session has it in the Dock.
- **The report says it was live**: boot medium (USB, its link speed), the overlay in RAM, no
  persistence. Live and installed results are kept apart in comparisons and in the catalogue.
- **Different references where live changes the numbers**: idle memory is compared with the
  paper's live figure (465 MB) and the overlay's size is subtracted and shown; boot time includes
  reading from the stick and is reported, not judged.
- **Nothing written to the computer's disks.** Internal disks are detected and their SMART health
  read; the storage test only runs on the live overlay in RAM and, read-only, on the USB stick, and
  says so. Measuring the internal disks waits for an installed system.
- **Where the report goes:** with no persistent home, the app saves it to a removable drive the user
  picks (a second stick, or a writable partition on the same one if present) or shares it directly
  through the browser when there is a network connection.
- **Missing firmware and drivers** are especially valuable here: the report lists what the ISO
  lacked for this machine (Wi-Fi, GPU firmware), which tells us what the ISO should carry.
- **The installer reuses the same checks** (`server/src/sysreport/`): before installing it shows
  the Compatibility summary (for example "GPU: not used, software renderer" or "Wi-Fi: firmware
  missing") so the user knows before replacing their system.
- **Limit:** machines with Secure Boot on can't start the stick until LordOS is signed for it
  (paper §10.2); the website's download page says so next to the *Check this computer* guide.

## New interfaces, all under the rules of `AGENTS.md`

- `lord.sysinfo` in `server/src/ui/js_hw.c`: `scan()`, `checks()`, `report()`; in 0.8.6
  `bench.start(kind)`, `bench.poll()`, `bench.cancel()`.
- `lordos-sysreport`, a user command (not started by `lord-server`).
- One read-only admin action, `hw-memory-modules` (anyone, no arguments): SMBIOS memory device
  entries parsed strictly in C, returning slot, size, type, speed and manufacturer only.
- The probe app for the effective checks: a signed system package started through the existing
  `lord.apps.launchPackage(id)`; no new launcher path.
- `lord-bench` (0.8.6), as above.
- Data packages `pci.ids` and `usb.ids` (Debian, architecture `all`), documented in `VM-SETUP.md`.

## Work plan for 0.8.5

**Built (2026-10-26):** `server/src/sysreport/` (the collector, `ids.c` for `pci.ids`/`usb.ids`, the JSON
and Markdown output), `lordos-sysreport report [--json | --markdown]` in `/usr/bin` (package `lordos`,
which recommends `pci.ids` and `usb.ids`), and `tests/sysreport_test.c`: five fake machines (AMD desktop,
Intel laptop, Nvidia desktop, Raspberry Pi 5, VM) full of serials, UUIDs, MAC addresses, a host name, a
user name, monitor serials and a MAC inside an interface name, none of which may reach a report
(`build/sysreport-test fixture <dir> <kind>` writes one for `--root`). Checked on VM 116 against the real
machine. `lordos-fetch` (the same binary under that name; `lordos-sysreport fetch`) shows the crown in
the brand colors beside a short summary (system, computer, kernel, uptime, packages, shell, display, CPU,
GPU, memory, disk, isolation); the user and host names appear only on screen and `--share` leaves them out
for a screenshot to post; `--color=auto|always|never`, `NO_COLOR` respected, true color when `COLORTERM`
says so, plain ASCII without color. **The app (2026-10-26):** `apps/sysinfo` (also About LordOS → System Report…) with Overview, Hardware and
Compatibility, from `lord.sysinfo.report()` (`ui/js_hw.c`): the same report plus `session`, how this
session composes the screen now (`lord_composition()` in `main.c`: `gpu`, `headless`, `no-display`,
`rotated`, `capture` or `gpu-off`) and the GPU context (`gpu_info()`: the last `gpu_init` outcome as a
fixed code, the renderer, render node, dmabuf import and direct scanouts since start). The layers and
the Compatibility panel are worked out in `apps/sysinfo/model.js` (`server/tests/sysinfo_model_test.mjs`).
Checked on VM 116 in an isolated headless session (own home, runtime folder and bus):

![Overview](sysinfo-overview.png)

![Graphics as separate layers](sysinfo-graphics.png)

![Compatibility](sysinfo-compatibility.png)

**Report tab (2026-10-26):** the exact text that would be saved, as Markdown (`lord.sysinfo.markdown()`:
`lordos-sysreport`'s text plus a "This session" section) or JSON (the report as `lord.sysinfo.report()`
gives it); **Copy** and **Save to Documents** (`lordos-report-<date>.md|json`, `-2`, `-3`… never over an
existing file), with **Show in Files**. Nothing is sent anywhere: publishing comes with 0.8.7.

![The Report tab](sysinfo-report.png)

**Memory modules (2026-10-26):** Hardware → Memory → **Read modules** runs the read-only admin action
`hw-memory-modules` (the SMBIOS type 17 entries only root can read, parsed in `sysreport/smbios.c`:
slot, bank, size, type, speeds, manufacturer; never serial, asset tag or part number). On VM 116 the
parser read `mem 4096|RAM|0|0|DIMM 0||QEMU` from QEMU's real tables (`build/sysreport-test smbios` as
root); the button itself was not exercised on screen (the isolated test session has no admin helper,
so it shows disabled):

![Hardware → Memory](sysinfo-memory.png)

**Checks, part 1 (2026-10-26):** what `lord-server` can see itself, evaluated in `apps/sysinfo/checks.js`
(the thresholds are constants there, so they are signed with the theme bundle instead of a separate
`checks.json`), from `lord.sysinfo.facts()` (`sysreport/facts.c`: lord-server's uid/euid from
`/proc/self/status`, its cgroup and who owns it, the app scopes beside it, and whether anything
listens on port 5673 in `/proc/net/tcp{,6}`): display server without root (§2.2), Landlock ABI ≥ 6
(§6.4), `lord-netperm` running with cgroup BPF and the BPF LSM (§6.6), verified system code
(effective: every bundle loaded this session matched; §6.9), remote desktop closed when off
(effective: nothing listens on its port; §11.4), the session's cgroup tree owned by root (only inside
`…/lordos-session-<uid>/server`; §11.4), composition on the GPU (§7.3) and lord-server's memory now
(measured, no pass or fail; §7.1). On VM 116 it found two real things: `lord-netperm` isn't installed
there (the unit doesn't exist), and the first run refused a stale `app-host.bundle.js` in the test
tree. Part 2 is below.

![Checks, part 1](sysinfo-checks.png)

**Checks, part 2: the Sandbox Probe (2026-10-26, approved by the maintainer).** A system package,
`sysprobe` (`apps/sysprobe`, built into `assets/sysapps/sysprobe` like Passwords, hidden from the launcher
with the new manifest field `"hidden": true`), started with the existing `lord.apps.launchPackage(id)`
like any isolated app: no new argument, path or exec. In app mode (`app/appclient.c`), for that system
package only, `app/probe.c` runs instead of the app's code:

1. **Before the app's sandbox**, while the process is single-threaded and has loaded nothing, it forks a
   child that takes the decoding helpers' profile (`sandbox_apply_helper`, as `lord-doc` and `lord-media`)
   and tries a unix socket, exec, fork and the app's data folder.
2. It applies **the same sandbox as any isolated app** (same read-only folders, no network) and tries
   what must fail: the home folder, `/etc/passwd`, the root cgroup's `cgroup.procs`, an internet and a
   unix socket, `ptrace`, a user namespace, `mount`, `exec`. Its own data folder must work (the control:
   a probe that can't write there reports nothing it can be trusted for).
3. It asks its broker for what must be refused: a path outside its folder, the clipboard without
   permission, the FIDO2 device, `lord-maild`.
4. It sends the result as `BOP_PROBE` and exits, without a window. `brokersrv.c` takes `BOP_PROBE` only
   from that system package, validates the JSON (≤ 8 KB) and adds what only it can read: the process's
   `NoNewPrivs`, `Seccomp` and `CapEff` and its cgroup with `memory.max`. `lord.sysinfo.probe()` returns
   the last result; `apps/sysinfo/checks.js` (`evaluateProbe`) turns it into four rows: the app sandbox
   (§6.4), the helper sandbox (§6.7), the broker (§6.5) and the app's own capped scope (§6.4).

On VM 116, in the isolated headless session, every try was refused (9 + 4 + 4) and the process reported
no_new_privs, seccomp mode 2 and no capabilities; the scope row is a warning there because that session
isn't in a LordOS session tree (lord-sessiond refused the scope, as it must). Its first run also caught a
mistake in the probe itself: stacking the helpers' profile inside the app's sandbox is refused by the
app's seccomp (rightly), so the helper child now starts before it.

![The Sandbox Probe](sysinfo-probe.png)

Not yet: a connection refused by the per-app firewall (it needs an app with network access and a
temporary rule), video decode and encode and Vulkan (shown as *Not checked*).

1. `server/src/sysreport/`: collector, GPU layers, `pci.ids`/EDID readers, fake-sysfs fixtures
   (AMD desktop, Intel laptop, Nvidia desktop, Raspberry Pi 5, VM), the anonymization test,
   `hw-memory-modules`, `lordos-sysreport report`.
2. The app: Overview, Hardware, Compatibility; About's button; en, es, pt; right to left.
3. Checks, configured and effective, the probe app, signed `checks.json`, `lordos-sysreport checks`.
4. Reports with preview; docs; screenshots on VMs; the first real reports: the Raspberry Pi 5 with a
   monitor and the maintainer's PC (Core i9-10900, Radeon RX 580), the first real AMD data in the
   project.
