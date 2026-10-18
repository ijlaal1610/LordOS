## The short version

Most firewalls think in terms of ports and addresses. LordOS's firewall thinks in terms of **apps**: you decide which of your apps may use the network, and how much of it. And because it is built into the Monitor, you can see what each app is doing before you decide.

[[shot:0]]

Picture a building with a guard at every apartment door instead of one at the street. When an app tries to open a connection, the guard at its door checks its list in a fraction of a microsecond and either lets it through or answers "not permitted". The guard sits inside the Linux kernel, so a determined app can't simply walk around it.

[[diagram:path]]

## What you see and decide

- **Monitor → Network** lists the apps with connections, how much each one sent and received, and a world map with an arc to each country they talk to. Each app has a **Rule** switch: turn it off and the app is denied the network at once, with allowed and denied counters coming straight from the kernel.
- **Settings → Privacy → App network access** has a mode and two lists:
  - **Silent** (the default): whatever has no rule is allowed and recorded, so you can review it in the Monitor. **Off**: only your explicit denials and restrictions apply.
  - **Trusted apps**: allowed without recording. An explicit denial you set still wins.
  - **Restricted apps**: tick what an app may not do. The network restrictions are **no internet** (LAN only), **no LAN**, **no network at all** and **no incoming connections**.

[[shot:1]]

[[shot:2]]

## How it works

**1. Every app has a name the kernel can see.** When you open a Linux app from the app launcher, `lord-server` asks root to place it in its own cgroup, `app-lordos-<id>.scope`. Installed `.lpk` apps get `app-lordos-lpk-<id>.scope`. These scopes are created and owned by root, so an app can't move itself to another one.

**2. A small root service enforces the rules.** `lord-netperm` loads seven eBPF programs into the cgroup of the LordOS service, which contains every session:

| Kernel hook | What it checks |
|---|---|
| `connect4`, `connect6` | TCP and connected UDP |
| `sendmsg4`, `sendmsg6` | UDP sent with an address |
| `bind4`, `bind6` | incoming: "no incoming connections" and inbound rules |
| `sock_create` | "no network": the app can't even create an internet socket |

Each program finds which app the calling process belongs to (by the cgroup ancestor at the scope level, so child processes count as the same app), decides, and returns allow or deny. A denied app gets `EPERM`, "Operation not permitted".

[[shot:4]]

**3. Your rules become kernel maps.** You change rules in the session; `lord-server` saves them to `~/.config/lordos/firewall.json` and sends `lord-netperm` a single message with no data: `RELOAD`. The service then reads the file itself, strictly validated, and compiles the rules into hash and longest-prefix-match maps. The rules never travel over the socket.

**4. Nothing runs per packet.** The checks run when a connection is opened or a port is bound, not for every packet. Measured on a test VM: about **0.5 µs** per UDP connect, TCP within noise, and no change in throughput (`docs/FIREWALL.md`).

## Does it work per domain?

The engine does, with care about where names come from. A connection only carries an IP address, so to apply a rule like "allow only `example.com`", the firewall must know which names an address belongs to. It learns that **only from the system resolver**: it subscribes to systemd-resolved's monitor (or, without it, reads replies from port 53 of the servers in `/etc/resolv.conf`). It never trusts DNS answers an app received on its own sockets, which a malicious app could forge.

[[diagram:dns]]

On the test VM, "deny everything except example.com" let `https://example.com` through and denied `https://www.debian.org`, 20 out of 20 times even with an empty resolver cache. An app that uses its own DNS-over-HTTPS gets addresses the firewall never learned, so under such a rule they are denied.

What is **not** there yet: a screen to write domain rules. The rule engine and the `lord.fw.addRule` API support domains, wildcards, networks, ports and protocols, but the interface today offers the per-app switch and the restriction lists. Per-destination switches in the Monitor are planned.

## The skeptical reader's questions

**Does it apply to Linux apps too?** Yes. That is one of its main points: Chromium, Firefox or any app opened from the app launcher has its own scope and its own rules. Built-in LordOS apps and programs started from the Terminal count together as "LordOS".

**Can an app escape its scope?** It was tried. A Linux app can't write itself into another cgroup, because every `cgroup.procs` file in the session's tree belongs to root. It can still ask the user's systemd manager to start a new process outside the tree (`systemd-run --user`). LordOS closes that too: `lord-netperm` also attaches its programs to each user's `user@<uid>.service`, and any process there is held to the strictest union of the restrictions of that user's running restricted apps. Tested with `systemd-run --user --scope`, a transient service and `setsid`: all denied.

[[shot:3]]

**Can I allow a connection just once, or be asked?** The rule engine knows three durations (forever, until the app quits, just once) and an "ask" mode, but the ask prompt isn't built yet: today "ask" behaves like silent. It is designed as a retry prompt: the first connection is denied, you get *Allow once / Always / Deny*, and the app's retry goes through.

**What happens if the service stops?** Its links are closed and everything is allowed. The desktop doesn't depend on it. On the final ISO the service was running with its seven programs attached.

**Is the service itself dangerous?** It is kept small on purpose: it runs no programs, takes no arguments, has no internet sockets of its own (its unit only allows Unix and packet sockets), and accepts one message type. It uses its own loader over the `bpf()` system call, with the programs compiled into the binary; no libbpf or bcc.

## Honest limits

- **ICMP** (ping) is only blocked by "no network". **Incoming connections to a port that is already open**, and `listen` without `bind`, aren't checked: that would need a per-packet hook.
- **The `user@` union is coarse:** while a restricted app runs, every process in that user's systemd manager is held to the union of restrictions.
- **A brand-new scope** is registered within microseconds, but a connection made before that would pass under default rules.
- **Host names aren't shown in the Monitor yet**, only IPs and countries. The Monitor never does reverse DNS, so no address leaves the machine just because you looked at it.
- **Non-network restrictions** (camera, microphone, screen, USB, Bluetooth, files, notifications) can be ticked in Settings but **aren't enforced yet**: only the four network restrictions are. Use the per-app permissions for the camera and the microphone.
- **The rules file can be edited by unsandboxed Linux apps**, since they run as you. Installed `.lpk` apps can't reach it.

## Read the source

- [server/src/netperm/np.bpf.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/np.bpf.c): the seven cgroup eBPF programs
- [server/src/netperm/netperm.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/netperm.c): the root service
- [server/src/netperm/npcompile.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npcompile.c): rules into kernel maps
- [server/src/netperm/npdns.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npdns.c): names from the system resolver
- [server/src/fw/rules.c](https://github.com/lordbasex/LordOS/blob/main/server/src/fw/rules.c): rules, precedence and the strict file reader
- [server/src/ui/js_fw.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/js_fw.c): `lord.fw`, the session API
- [apps/monitor/Firewall.jsx](https://github.com/lordbasex/LordOS/blob/main/apps/monitor/Firewall.jsx): the Settings section
- [server/tests/netperm_integration.sh](https://github.com/lordbasex/LordOS/blob/main/server/tests/netperm_integration.sh): the kernel test, 18 of 18
- [docs/FIREWALL.md](https://github.com/lordbasex/LordOS/blob/main/docs/FIREWALL.md) and [docs/MONITOR.md](https://github.com/lordbasex/LordOS/blob/main/docs/MONITOR.md)
