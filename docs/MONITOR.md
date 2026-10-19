# Monitor

Preinstalled native app (`apps/monitor`) to see how this machine is being used: CPU, memory,
network, disks, processes and each app's network connections on a world map. It copies the look
of TangoOS's Monitor (`tangoos/web/packages/app-monitor`): segmented tabs, dark cards with an icon,
a big number and a live area chart, a row of small figures and a footer with the sampling rate.
The Network tab takes the shape of a per-app network monitor (apps on the left, map in the middle, summary on
the right).

Phase 1 was read-only. Since then the Network tab holds per-app firewall rules (Phase 2), in
0.8.1 the Processes tab can stop a process and show its details ("Process actions"), and in 0.8.2
each destination of an app has its own allow/block switch and the traffic chart reaches back 30
days ("Traffic history"). There are no alerts.

## Tabs

| Tab | What it shows |
|---|---|
| Overview | CPU, Memory, Network and Disk with a 60 s chart; below, CPU temperature, fan, 1 min load, 5/15 min load and cached memory |
| CPU | total usage (big chart), one chart per core, 1/5/15 load, temperature and tasks |
| Memory | stacked bar (used, file cache, free), swap and a 60 s chart |
| Network | apps with connections, map, summary or detail of the chosen app (see below) |
| Disks | total read and write and one row per disk (model, `/dev`, size, rates) |
| Processes | sortable table (CPU, memory, threads, PID, user), search, "Mine" / "All"; Stop and Get Info for the selected process (see "Process actions") |

Charts pause with the mouse over them and show a crosshair with the values at that instant, as in
TangoOS. Time always runs left to right, also in Arabic and Hebrew; the rest of the window mirrors
itself.

### Network

- **Left:** search (by app, address or country) and one row per app with its icon, number of
  connections, a traffic bar (received in green, sent in orange, relative to the app that moved
  the most), a tiny live activity indicator (its traffic over the last 12 s, one bar per second),
  the bytes and the **Rule** column: a per-app allow/deny switch that `lord-netperm`
  enforces in the kernel (see "Phase 2"). Each app expands to show its destinations, one row per
  name the system resolver gave (its addresses together) or per address, with the country, the
  ones the firewall reported recently ("recent connection" or "recently denied") and, since 0.8.2,
  **each one's own allow/block switch** (see "Per-destination switches"); a click on a row selects
  it in the right panel. At the bottom, the machine's traffic:
  - the current download and upload speed;
  - two pills with what was sent (↑, orange) and received (↓, green) in the visible minute;
  - a **mirrored chart** of the last 60 s (`TrafficChart.jsx`, `drawMirror` in `draw.js`): one bar
    per second, sent above the baseline and received below, every bar on a faint full-height track
    so the strip reads as one continuous chart even when idle; the newest second grows in from the
    right edge while the rest slides left, redrawn at the 4 samples per second with the window in
    front (once per second behind it). Auto-scaled to a "nice" maximum shown at both ends; any
    bucket that moved bytes is at least one pixel tall;
  - since 0.8.2, a zoom above the chart: **60 s** (the live minute), **1 h**, **24 h**, **7 d**
    and **30 d**, with ‹ › to step one window back or forward and **Now** to come back (see
    "Traffic history").
- **Center:** the map fills the whole height of the column. It has an arc from "This computer" to
  each country, one dot per country (bigger the more connections) and pill labels with the name
  and the count. It's controlled like this:
  - wheel: zoom toward the cursor;
  - touchpad: pinching zooms toward the center of the gesture and two fingers move the map (see
    `TRACKPAD.md`; with remote desktop, the browser's trackpad pinch works too).
    Screenshots: [before](trackpad-map-before.png) and [after pinching](trackpad-map-after.png);
  - drag: move;
  - double click: zoom in;
  - `+`, `−` and "show all" buttons in the corner.

  When zoomed in, the borders and names of the other countries show. The Lite base has no cities,
  so labels are always countries. (0.8.2: city labels would need a city-level IP database with a
  compatible license; Debian 13 only ships country-level ones, `geoip-database`, so none was added.)

  **Direction of the traffic.** While bytes move, dots travel along each arc: sent (orange) from
  "This computer" to the place, received (green) back. The higher the rate, the more dots (1 to 4
  each way) and the faster they go (3.4 s to 1 s per trip, on a log scale from 256 B/s to about
  4 MB/s). An idle arc has no dots and is drawn dimmed (`idle` in the map request, `map.c`). The
  dots are small ready-made images (a ring and the direction's color, drawn once) moved with
  `lord.setStyle`, no React render per frame: 12 frames per second with the window in front, 5
  behind it, at most the 12 busiest arcs and only those on screen; the timer stops with the window
  minimized, while the map is being moved and when nothing moves. Each frame repaints only the
  small rectangles the dots left and entered (0.8.2, "Several damaged rectangles"). With Appearance → Reduce motion, each busy arc shows two still markers instead: a short
  comet of three dots, the biggest in front, pointing the way the bytes go.

  When an app is chosen, the map shows only its places and frames them. A click on a dot or an arc
  chooses that country: the right panel shows its apps, addresses, ports and bytes. At the bottom,
  a line with the DB-IP attribution and the link to the privacy settings.
- **Right** (its scroll area keeps an 11 px strip for the scroll bar, `ScrollView gutter`, so the
  bar never covers a value; long values wrap):
  - no selection: traffic (current and since boot), allowed and denied connections (0: there are
    no rules yet), incoming, listening and from other users, and the apps and destinations with the
    most traffic, each with "↓ received · ↑ sent" (destinations by the name the resolver gave,
    when known);
  - with a history zoom chosen (0.8.2): also "Most traffic · 24 h" (or the zoom chosen), the apps
    that moved the most in that window, from the history file;
  - with an app chosen: executable path, user, PIDs, rule, a "Rules for this app" link (Settings
    → Privacy with that app open), bytes and speed with the app's own mirrored chart of the last
    minute (one bar per second, or the history window chosen), ports and protocols, listening ports and
    addresses. A selected address offers **Allow / Block this domain** (or "this address" when no
    name is known) for that app: a sheet prefilled with the destination (see `FIREWALL.md`, "Rules
    by app");
  - with a country chosen: bytes, apps and addresses (with ports and app).

Hosts are shown as IPs. There is no reverse DNS: asking for an IP's name sends it to the DNS
server, and this app's rule is that no address leaves the machine.

**"This computer"** is placed by the time zone (`/etc/localtime` → coordinates from
`/usr/share/zoneinfo/zone1970.tab`), without the network.

### Per-destination switches (0.8.2)

An open app row lists the destinations it reached and gives each one an allow/block switch
(green: allowed, red: blocked), the same per-app rules Settings → Privacy → Network rules by app
edits (`FIREWALL.md`, "Rules by app"); there is no new enforcement:

- **What a row is:** one per name the system resolver gave for the app's addresses (from
  `lord-netperm`'s notices, `hosts`), with its addresses together ("cdn.example.net (4)"), or one
  per address when no name is known. The first eight show, then "N more…" (and "Show fewer").
  While the row is open the order is kept: busiest first when it opens, newcomers below, so a
  switch never moves under the pointer.
- **What the switch shows** is what the app's rules decide for that destination right now:
  `sdk/fwrule.js` `decide()` runs the same order as `fw_decide` (restrictions, then the most
  specific matching rule; no rule: the mode, which allows). When another rule decides, the row
  says which ("By the rule \*.example.com", "Follows this app's default"); a restriction ("no
  internet") locks the switch and says so.
- **What a click does** (`toggleOp()`): it writes the rule for exactly that name (or address),
  any port and protocol, with the other verdict; if that name already has its own rule it is flipped,
  or removed when the app's other rules alone already give the new verdict, so going back and forth
  never piles up rules. It goes through `lord.fw.addRule` / `updateRule` / `removeRule`, which save
  `firewall.json` and send `RELOAD`; `lord-netperm` applies it to the next connection.
- Domains match the names the system resolver returned (`FIREWALL.md`, "What domain rules can't
  see"): a browser using its own encrypted DNS shows addresses, and those rows write address rules.
- **Tests:** `server/tests/fw_domain_cases.txt`, 37 cases (exact names, wildcards, CNAME chains,
  networks, ports, protocols, LAN/internet, restrictions, IPv6), run through `fw_decide` by
  `build/fwdest-test` — once from the store and once after a round trip through the
  `firewall.json` text — and through `decide()` by `fwrule_input_test.mjs`, which also checks the
  switch's add / flip / remove choices. On VM 119 a click on a Firefox ESR destination wrote
  `{"app":"desktop:firefox-esr","dst":"scripts.webcontentassessor.com","action":"deny"}` and
  `lord-netperm` logged the reload.

### Traffic history (0.8.2)

The chart's zoom reads a history lord-server keeps: bytes received and sent **per minute for 7 days
and per hour for 30 days**, for the machine (every interface but `lo`, as the live totals) and for
each of the user's apps (`lord-netperm`'s per-app counters, the ones the live list uses).

| Zoom | Window | One bar |
|---|---|---|
| 60 s | the live minute (as before) | 1 s |
| 1 h | 1 hour | 1 minute |
| 24 h | 24 hours | 15 minutes |
| 7 d | 7 days | 1 hour |
| 30 d | 30 days | 6 hours |

‹ and › move one window back or forward (no further back than the oldest record), **Now** comes
back to the present; a window that reaches the present is read again every minute. With an app
selected its own chart follows the same window. Bars are average rates over the bar, the pills the
window's totals.

**Privacy.**

- **Whose file:** the apps are the user's own, so the history is the user's:
  `$XDG_STATE_HOME/lordos/net-history.bin` (default `~/.local/state/lordos/net-history.bin`),
  mode 0600 in a 0700 folder, written by the user's own lord-server. No root process writes into
  a home folder, and the machine's total is recorded only while that user's session runs.
- **What it holds:** app ids (`desktop:firefox-esr`, `lpk:…`, `lordos`) and byte counts. **Never an
  address, a domain name or a country**, whatever the "connections" switch says: the long-term
  file can't be used to rebuild where the machine went, only how much each app moved. (The live
  lists keep showing addresses and names while the Monitor is open and "connections" is on.)
- **Switch and Clear:** Settings → Privacy → Monitor → **Keep a traffic history** (on by default;
  `settings.monitor.history`) stops recording at once; **Clear History** deletes the file and what
  is in memory. Nothing leaves the machine.
- The login screen and isolated apps never record (lord-server only allows it in a user session).

**How it is written** (`server/src/monitor/netrec.c`, `nethist.c`):

- every 5 s lord-server reads `/proc/net/dev` and the latest per-app counters `lord-netperm`
  already sends it (`COUNTS`, read without waiting from the same socket `lord.fw` uses), and adds
  the difference to the minute in progress (in memory: its totals and one entry per app that
  moved bytes);
- when the minute ends it **appends one record** to the file, a few dozen bytes in one `write`;
- at startup and every 6 hours a **compaction** folds minutes older than 7 days into hours, drops
  hours older than 30 days and any damaged tail, and replaces the file atomically (a temporary file,
  `fsync`, `rename`). 40 days of one record every 10 minutes compact to under 64 KB (`nethist-test`); a
  week of minutes with a few apps is a few hundred KB.

**File format.** `"LNH1"`, a version byte and three reserved bytes, then records
`[type u8][length u16][payload][CRC-32]`; payloads use LEB128 numbers. `APP` gives an app id an
index; `MIN` (Unix time / 60) and `HOUR` (Unix time / 3600) hold the totals and `(index, rx, tx)`
per app. The reader checks every length, every varint and every CRC and stops at the first record
that doesn't check out (a crash in the middle of a write): what came before is kept and the next
compaction drops the rest. A file that isn't ours, that isn't a regular file of the user's or that
is bigger than 8 MB is not read; a symlink in its place is refused.

**Cost.** One small file read and a look at a buffer every 5 s, one append per minute: nothing
measurable next to the session (the main loop wakes every 5 s for it, only while recording is on).

`lord.monitor.history({ from, step, n, app })` returns `{ res, oldest, now, enabled, rx: [n], tx: [n],
top: [[app, rx, tx]] }`: bytes per bucket (`from` and `step` in Unix seconds, `n` ≤ 1500), for the
machine or one app id; `res` is the finest resolution found (60 or 3600 s), records longer than a
bucket are spread over it. `historyEnable(on)` and `historyClear()` go with the setting and the
button.

**Tests:** `build/nethist-test` (in `make -C server test`): totals survive writing, reopening and
bigger buckets; the top apps; 40 days of records rotate to 7 days of minutes and 30 days of hours
(totals checked exactly); a cut at every byte of a file and 2,000 random byte changes never crash,
keep what came before the damage and leave a clean file after the next write; a foreign file is
replaced; more than 1,024 apps go to "other"; Clear removes everything and recording goes on; a
symlink is refused. Also run with `-fsanitize=address,undefined`.

### Process actions (0.8.1)

Select a row in Processes and use the two buttons next to "Mine / All", the right-click menu, a
double click or the keyboard:

| Action | Toolbar | Menu | Keys |
|---|---|---|---|
| **Stop** | circle with a cross | "Quit Process…" | Delete (or ❖/Ctrl+Backspace) |
| **Get Info** | circled "i" | "Get Info" | ❖/Ctrl+I, Enter, double click |

The arrow keys move the selection. Both buttons are disabled while nothing is selected.
❖/Ctrl+Q is not used: it quits the Monitor.

**Stop** opens a sheet: *Are you sure you want to quit "name"?* with **Quit** (SIGTERM: the process
is asked to close), **Force Quit** (SIGKILL) and **Cancel**.

- **The user's own processes** (same uid) are signalled by `lord-server` itself with a syscall,
  not a program: `lord.monitor.signal(pid, start, 'TERM' | 'KILL')` (`js_monitor.c` →
  `proc_signal()` in `monitor/procctl.c`).
- **Another user's or root's processes**, only for administrators (sudo/admin/wheel): the root
  helper's fixed action `proc-signal <pid> <start> TERM|KILL` (`monitor/proc_admin.c`), the same
  authorization as every other privileged action (`admin.c` refuses a non-administrator with
  `admin.notAdmin`). The arguments are two decimal numbers and one of two words, checked on the root
  side; there is no shell and no program. A non-administrator gets an explanation instead of the
  buttons.
- **No PID-reuse races.** Every row carries `st`, the process's start time in clock ticks (field 22
  of `/proc/<pid>/stat`). `proc_signal()` first pins the process with `pidfd_open`, then compares
  the start time, and only then calls `pidfd_send_signal` on that pidfd. If the process ended and
  its PID was given to another one before the pin, the start time differs and the request is
  refused (`proc.gone`); if it ends after the pin, the signal can only reach the pinned (dead)
  process. Kernels without pidfd fall back to `kill(2)` right after the check.
- **Protected processes** can't be stopped from the Monitor, in either path (`proc_protect_reason()`;
  each row has `prot`, and the C side checks again). The sheet explains why instead:

  | `prot` | Which | Explanation |
  |---|---|---|
  | `init` | PID 1 | "This process starts and runs the whole system." |
  | `kernel` | kernel threads (`PF_KTHREAD`) | "This is part of the Linux kernel." |
  | `session` | the session's own `lord-server` | "This is your LordOS session. To end it, use Log Out…" |
  | `system` | `lord-sessiond`, `lord-netperm`, `lord-netd`, `lord-fand`, `lord-vault`, `lord-maild`, `lord-doc`, `lord-media`, the root helper (`lord-admin`), any `lord-server` that is not an isolated app (another session, a terminal holder), PipeWire (`pipewire`, `pipewire-pulse`, `wireplumber`) and the systemd managers | "This process is part of LordOS." |

  An isolated `.lpk` app (`lord-server --app <id>`) is not protected: a hung app can be quit. The
  names are matched against the process name and, when it can be read, the executable's name. This
  is a guard against mistakes, not a security boundary (the kernel's rules on who may signal whom
  still apply; a terminal can always `kill` the user's own processes).

**Get Info** opens a sheet that follows the process once per second while it is open:

- **Header:** icon, name, PID and user; the executable path and the command line (arguments with
  spaces are quoted; up to four lines, then "…"; at most 128 arguments of 2 KB each are read).
- **Memory:** real (RSS), virtual, shared, private, proportional share (PSS) and swapped out, plus
  the real memory by kind (anonymous, mapped files, shared memory objects). From
  `/proc/<pid>/status` (anyone can read it) and `/proc/<pid>/smaps_rollup` (the owner or root).
- **Statistics:** user (and the effective uid if it differs), parent process (a link: it selects the
  parent and shows its details), process group and session, CPU %, CPU time, threads, context
  switches (voluntary and forced), page faults (minor and major), start time, nice and priority, the
  control group (the app scope, as the firewall sees it) and the restrictions LordOS knows about:
  an isolated LordOS app (Landlock, seccomp, its own cgroup), a seccomp filter, "no new
  privileges", a network block rule and the app's restrictions from Settings → Privacy (`lord.fw`).
- **Open files and ports:** each descriptor of `/proc/<pid>/fd` (up to 300; the total is always
  shown): files, devices, pipes, kernel objects and sockets, with the protocol, local and remote
  address and TCP state from that process's own `/proc/<pid>/net/{tcp,tcp6,udp,udp6,unix}`. Same
  privacy rules as the rest of the Monitor: addresses are numbers, nothing is looked up on the
  network, and with Settings → Privacy → Monitor → connections off the remote addresses are hidden.
- **Another user's process:** what anyone can read (status, stat, cmdline) comes from the session;
  the memory breakdown, executable, working folder and open files need root. For an administrator
  they come from the helper's read-only `proc-info <pid> <start>` action (the same JSON, sent as LOG
  lines of at most 900 bytes that `sdk/monitor.js` joins); for anyone else those fields say "Not
  available" or "Only an administrator can see this".
- **Quit…** opens the stop sheet for the same process; **Close** (or Escape) closes it. If the
  process ends, the sheet says so.

The details are built on the Monitor's sampling thread (`mon_detail`, once per second and only
while the sheet is open), never during a frame. Sampling a process's stacks ("Sample process") is
not offered: it needs ptrace and an unwinder, which LordOS doesn't ship.

Tests: `build/procctl-test` (in `make -C server test`): stat parsing with a hostile name, the
protected list, a stale start time refused while the process survives, TERM and KILL on real
children, the session and PID 1 refused, and the Get Info JSON (a pipe and a listening TCP socket
among the open files; no private parts when not the owner; "gone" for another start time).

## Architecture

```
apps/monitor/            index.jsx (tabs), Network.jsx (Network), MapView.jsx (map), Chart.jsx,
                         TrafficChart.jsx (mirrored sent/received chart), History.jsx (zoom and
                         history windows), draw.js (pixels),
                         icons.jsx, Privacy.jsx, countries.js (generated)
apps/monitor/Processes.jsx   the Processes tab, its context menu, the stop sheet and Get Info
sdk/monitor.js           useMonitor(), stores monitorSamples / monitorProcesses / monitorConnections /
                         monitorDetail; monitor.watchProcess(), monitor.stopProcess()
server/src/ui/js_monitor.c   lord.monitor (read-only, validated API)
server/src/monitor/mon.c     sampling thread: /proc, /sys, processes
server/src/monitor/conn.c    connections via sock_diag + inode → process
server/src/monitor/mmdb.c    our own MaxMind DB reader (IP → country)
server/src/monitor/map.c     vector map, on its own thread
server/src/monitor/nethist.c the traffic history file (append, query, compaction)
server/src/monitor/netrec.c  feeds it every 5 s from /proc/net/dev and lord-netperm's counters
server/src/monitor/procctl.c the protected list, proc_signal() (pidfd) and the Get Info JSON
server/src/monitor/proc_admin.c  the root helper's proc-signal / proc-info (administrators)
assets/monitor/          land.bin (Natural Earth 1:50m), geoip-country.mmdb.xz (DB-IP Lite)
tools/gen-monitor-data.py    generates that data, countries.js and the country names
tests/mmdb_test.c, tests/map_bench.c   reader test and map benchmark
```

### lord.monitor

| Function | What it does |
|---|---|
| `start()` / `stop()` | adds or removes an interested party; at 0, or with no queries for 3 s, the thread goes to sleep |
| `want(processes, connections, countries)` | which lists the thread builds besides the sample |
| `sample()` | the latest sample, or `null` if there is no new one |
| `processes(lang)` | `{ t, total, hz, list: [{ pid, ppid, uid, user, name, cmd, exe, state, threads, cpu, rss, start, st, self, prot, app, appName, appIcon }] }` (`st`: start time in ticks; `prot`: why it can't be stopped, or `null`) |
| `connections(lang)` | `{ t, ok, others, list: [{ id, proto, fam, state, dir, local, lport, remote, rport, scope, cc, pid, self, name, exe, sent, recv, app, appName, appIcon }] }` |
| `info()` | `{ cpuModel, cores, geo, tick, home: { zone, cc, lat, lon } }`; `cpuModel` comes from `js_sys_cpu_name()`, the same one About uses (on ARM, the core from "CPU part" and the board) |
| `map(view)` | requests a map frame: `{ width, height, lon, lat, ppd, land, border, accent, homeColor, selColor, home, dots: [{ lon, lat, r, sel }] }` (validated numbers, up to 2048×2048 and 256 dots) |
| `mapFrame()` | the latest ready frame `{ seq, width, height, lon, lat, ppd, ms, data }`, or `null` |
| `detail(pid, st)` | Get Info: the process the thread describes once per second (`0, 0`: none) |
| `detailInfo()` | its latest details, or `null` if there is nothing new; `{ pid, gone: true }` once it ended |
| `signal(pid, st, 'TERM' \| 'KILL')` | stops one of the user's own processes: `null`, or an error code (`proc.gone`, `proc.protected…`, `proc.notOwn`, `proc.denied`) |
| `history({ from, step, n, app })` | the traffic history in `n` buckets (see "Traffic history"), or `null` |
| `historyEnable(on)` / `historyClear()` | the "Keep a traffic history" setting; deletes the history |

No function takes paths or commands: the only arguments are booleans, a language code (letters,
digits, `-` and `_`, up to 15), a PID with its start time (integers), `'TERM'` or `'KILL'`, and the
history's integers and app id (`[A-Za-z0-9._:@+~/-]`, up to 127).
`lord-server` runs nothing for the Monitor.

### Sampling without slowing the screen

- A dedicated thread (`mon.c`) builds the sample every **250 ms** (4 Hz, as TangoOS): `/proc/stat`
  (total and per-core CPU), `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime`, `/proc/net/dev`,
  `/proc/diskstats` (only whole disks from `/sys/block` with `device/`), and the temperature and
  fans from `/sys/class/hwmon` (chip `coretemp`, `k10temp`, `zenpower`, `cpu_thermal`…; if there
  is none, `/sys/class/thermal`).
- Processes (`/proc/<pid>/stat`, `cmdline`, and `exe` for the user's own) and connections: **once
  per second**, and only if the tab that shows them is open.
- The thread leaves the JSON ready; the screen thread only copies and converts it
  (`JS_ParseJSON`). It never reads files during a frame.
- **Zero cost when closed:** without `start()` the thread doesn't even exist; after `stop()` (or if
  nobody asks for a sample for 3 s, for example when switching themes) it sleeps on a condition
  variable, and releases the country database.

### Connections

`conn.c` does the same as `ss`: a `NETLINK_SOCK_DIAG` dump for TCP and UDP, IPv4 and IPv6, with
`INET_DIAG_INFO` (`tcpi_bytes_acked` and `tcpi_bytes_received` come from `tcp_info`). No root
needed. For other users' sockets, only how many there are is counted.

**Bytes per app (0.8.1).** `tcp_info` only exists for TCP and only while the socket is open, so up
to 0.8.0 UDP — and with it QUIC: HTTP/3, a browser playing a video — counted nothing, and a closed
connection took its bytes with it (a browser streaming music showed 13 KB). With `lord-netperm`
running, the bytes and rates of each app and of each of its addresses come from the kernel instead:
its `cgroup_skb` programs count every packet of the app's sockets, TCP and UDP, both directions
(`FIREWALL.md`, "Traffic accounting"), and its `COUNTS` notice brings, every second, the app's
totals since its scope appeared, its rates over the last second and its 64 busiest addresses
(`lord.fw.poll()`, `firewallState.counts`). An address whose socket already closed keeps its bytes
and its country (remembered from the last open connection to it). Without `lord-netperm` (or with
`STATUS` `bytes: false`) the Monitor falls back to `tcp_info`.

The owning process of each of the user's sockets comes from `/proc/<pid>/fd` (`socket:[inode]`) of
the user's own processes. That walk is the most expensive part, so there is an inode → pid cache
that is only rebuilt when an unknown socket appears, and at most once per second.

Direction: `listen` (TCP listening or UDP without a destination), `in` (the local port is a
listening one) or `out`. Scope: `internet`, `lan` (private, CGNAT, link-local, ULA, multicast) or
`local`.

**From process to app** (`js_monitor.c`, on the screen thread with the list from `apps.c`):

1. `lord-server` itself is **LordOS** (native apps and the Store run inside it);
2. otherwise, the `.desktop` whose `Exec` program (`DesktopApp.bin`) matches the executable,
   `argv[0]` or the process name;
3. otherwise, `StartupWMClass` equal to the process name;
4. otherwise, the process name.

### Country of an IP

- **Database:** DB-IP Lite Country (CC BY 4.0, <https://db-ip.com>), converted to the MaxMind DB
  format by `tools/gen-monitor-data.py`. To keep it small:
  - it only stores `country.iso_code`;
  - IPv4 blocks smaller than /24 and IPv6 blocks smaller than /32 go to the country with the most
    addresses inside;
  - it's compressed with xz.
- **Size:** 854 KB in the package (4.2 MB in memory). The DB-IP original is 8.3 MB.
- **Accuracy:** against the full DB-IP file, over 200,000 random addresses, it matches
  **99.96 %** for IPv4 and **99.78 %** for IPv6 (`build/mmdb-test <ours> <dbip.mmdb>`).
- **The reader** (`mmdb.c`) is our own:
  - every access goes through a bounds check;
  - a decoder pointer is followed only once;
  - depth is bounded;
  - a broken file gives "no data".
- **Test:** `build/mmdb-test` (in `make -C server test`) looks up known addresses and opens 400
  broken copies (truncated, with random bytes, fake metadata); during development it is also run
  with `-fsanitize=address,undefined`.
- **Loading:** the database is decompressed on the Monitor thread the first time connections with
  countries are requested. It is released when the app closes or when location is turned off.

No address is sent to any service: neither online geolocation nor reverse DNS.

### Map

The map is drawn as vectors, sharp at any zoom (no scaled PNG).

- **Data:** `assets/monitor/land.bin` (200 KB), from Natural Earth 1:50m, public domain.
  - The outlines are simplified (Douglas-Peucker, 0.02°) and in hundredths of a degree.
  - No Antarctica: the map goes from 84° N to 56° S.
  - `countries.js` has each country's label point (`LABEL_X`/`LABEL_Y`).
  - The country names in `assets/locales/{es,en,pt}/countries.json` come from
    `NAME_ES`/`NAME_EN`/`NAME_PT`.
- **Drawing** (`map.c`, on its own thread, equirectangular projection):
  - fill with an active edge table, 4 sub-scanlines per row and exact horizontal coverage
    (antialiasing), even-odd over all countries;
  - borders and a 30° grid (10° when zoomed) with Wu lines;
  - arcs and dots on top.

  Segments shorter than one pixel are skipped, and edges are sorted by sub-scanline with a
  counting sort, in O(n).
- **Without stalling:**
  - the screen thread requests a frame (`map`) and picks it up when it's ready (`mapFrame`),
    without waiting; if several requests arrive, only the last one is drawn;
  - meanwhile, the app shows the previous frame shifted, and scaled if the zoom changed, so
    dragging and zooming respond immediately;
  - labels are engine text, always sharp, and are placed without overlapping.
- **Memory:** after 20 s without requests, the thread releases the outlines and the buffers.

### Charts

`Chart.jsx` draws in pixels at the box's real width (`lord.getLayout`):

- gradient fill from 0.32 to 0.02 opacity and a 1.6 px line, as in TangoOS;
- guides at 25/50/75 %;
- axis with a "nice" scale (1, 2, 2.5 or 5 × 10ⁿ).

With `Uint32Array` it costs one pixel per write.

## Privacy

Settings → Privacy → **Monitor** (registered with `registerPrivacySection` if the page exists) has
three switches, saved in `settings.monitor`, and a button:

- `connections`: see each app's connections. When off, the thread doesn't query sock_diag.
- `geo`: place them on the map. When off, the country database isn't loaded.
- `history` (0.8.2, on by default): keep the traffic history (bytes per minute and per hour, never
  addresses; see "Traffic history"). **Clear History** deletes it.

## Phase 2

**Done (firewall, see `FIREWALL.md`):**

- The **Rule** column is a per-app switch (`lord.fw.setAppRule`). `lord-netperm` applies it in the
  kernel and a denied app gets EPERM.
- The **allowed** and **denied** counters in the summary and for each app come from the kernel.
- An app with no open connections but with denied attempts still shows up in the list.
- Each process is attributed to the scope of the app it runs in (`/proc/<pid>/cgroup`).
- **Per-destination rules:** "Allow / Block this domain" on an app's destination writes a rule for
  that app (domain, `*.domain`, address or network, optional port and protocol), and the app's
  rules are listed and edited in Settings → Privacy → Network rules by app. Domains match the names
  the system resolver returned; an app with its own encrypted DNS is held only by address rules.
  Screenshots: [light](firewall-domain-rules-monitor-light.png),
  [dark](firewall-domain-rules-monitor-dark.png).

**Planned:**


- **Connection alerts** (the "ask" mode, `FIREWALL.md`) and rules per country.
- **Devices:** `monitor.deviceUsers()` → `[{ kind: 'camera' | 'microphone', app, name, pid }]` and
  the `monitorDevices` store (already exported, empty).
- **Host names** without asking anyone: `lord-netperm` already learns them from the system
  resolver; its notices don't carry them to the session yet. The Network tab shows a `host` /
  `hosts` field as soon as a notice brings one.
- Usage alerts (the history is in place since 0.8.2).

## Measurements

VM 123 (4 vCPU Xeon Silver 4108 at 1.8 GHz, `virtio_gpu` without 3D, CPU compositing, 1280×800).

### lord-server CPU

Percentage of one core, 30 s average (`~/thr.sh`, per thread, from `/proc/<pid>/task`). During the
measurement there were 13 downloads in progress and Firefox open on the Network tab.

| Situation | Main thread | Sampling thread | Map thread |
|---|---|---|---|
| Monitor closed | 0.4–0.5 % | (doesn't exist / asleep) | (doesn't exist / asleep) |
| Overview, in front (4 charts at 4 Hz) | 10.4 % | 0.1–0.2 % | — |
| Overview, in the background (drawn at 1 Hz) | 4.7 % | 0.2 % | — |
| Processes, in front | 5.6 % | 0.7 % | — |
| Network, map still | 5.1 % | 0.5 % | 0 % |
| Network, dragging the map nonstop | 20.6 % | 0.6 % | 4.0 % |
| Monitor closed again | 0.4 % | 0 % | 0 % |

- **Closed costs nothing:**
  - without `start()` the sampling thread doesn't exist;
  - afterwards, it sleeps on a condition variable;
  - the map thread releases everything after 20 s.
- **Sampling:** reading `/proc` and `/sys` four times per second costs less than 1 %.
- **Drawing the interface:** this is what costs.
  - Each tab subscribes only to what it shows and at its own rate: charts at 4 Hz with the window
    in front and 1 Hz behind; Processes and Network at 1 Hz.
  - Before splitting it this way, Processes cost 17 % and Network 14 %.
  - In Overview, most of it is repainting the box that holds the four charts (the engine uses a
    single damaged region per frame, about 12 ms) and drawing the charts in JS (around 1.5 %).

### 0.8.1: traffic accounting, chart and moving dots (VM 118)

VM 118: 4 vCPU Xeon Silver 4108 at 1.8 GHz, `virtio_gpu` without 3D, CPU compositing, 1280×800.
Network tab in front, five downloads running (one over HTTP/3), arcs to 4–6 countries.
`lord-server` (whole process, % of one core), 3 × 30 s each:

| Build | Runs | Mean |
|---|---|---|
| 0.8.0 theme (bars redrawn once per second, no animation) | 5.2 / 6.0 / 8.2 % | 6.5 % |
| 0.8.1, Reduce motion (chart at 4 Hz, still markers) | 8.9 / 9.4 % | 9.2 % |
| 0.8.1, dots moving at 12 frames per second | 11.7 / 12.7 / 13.1 % | 12.5 % |
| 0.8.1, window minimized | 2.0 % | |

- **The chart** (redrawn 4 times per second with the window in front) costs about 2.5 points; behind
  other windows it drops to once per second.
- **The moving dots** cost about 0.3 points per frame per second (5 fps: 10.1 %, 15 fps:
  13.6 %): each frame repaints the box around all the dots, which with arcs across the world is
  most of the map. 12 fps is the compromise. (0.8.2: several damaged rectangles, see below.)
- An `lord.setStyle` that only moves an absolutely positioned node (the dots, the map image while
  panning) now just places it again instead of remeasuring every ancestor (`js.c`, like a window
  drag); it made no measurable difference here because painting dominates.
- **`lord-netperm`** stays at 0.0–0.07 % with traffic flowing (one map read per app per second,
  plus one batch read of the address table only while bytes move).
- **The kernel side** of the accounting is in `FIREWALL.md` ("Measurements").

### 0.8.2: several damaged rectangles (VM 119)

VM 119: 4 vCPU Xeon Silver 4108 at 1.8 GHz, `virtio` GPU without 3D, CPU compositing, 1280×800.
Network tab in front, ten slow downloads to mirrors in ten countries (arcs to 8–11 countries, about
20 moving dots). `lord-server` (whole process, % of one core), 5 × 30 s each, the same session
restarted with each build:

| Build | Dots moving | Reduce motion (still) | Animation's cost |
|---|---|---|---|
| before (0.8.1 engine and Monitor, `origin/main`) | 11.3 / 11.5 / 10.8 / 10.7 / 10.7 = **11.0 %** | 7.9 / 8.1 / 8.0 / 8.5 / 8.0 = **8.1 %** | **2.9 points** |
| after (this branch) | 10.1 / 9.2 / 8.8 / 9.2 / 9.8 = **9.4 %** | 7.8 / 7.9 / 7.9 / 8.1 / 7.6 = **7.9 %** | **1.6 points** (−46 %) |

What changed, and what each step bought (main thread, instrumented builds, per second of the dots
moving, about 14 frames per second):

| Step | Repainted per frame | Paint | Render (layout, damage, paint, present) |
|---|---|---|---|
| one damaged box (before) | 164,000 px, 1 rectangle | 25 ms/s | 33 ms/s |
| several rectangles (`dlist_add`, joined when the union wastes < 6,144 px) | 41,000 px, 5 | 21 ms/s | 29 ms/s |
| + clipped subtrees outside the rectangle skipped, cover search pruned | 45,000 px, 5 | 13 ms/s | 21 ms/s |
| + joined only below 1,024 px of waste | 30,000 px, 10 | 11 ms/s | 19 ms/s |
| + only the rectangles copied to the screen (`present_cpu`) | 26,000 px, 11 | 11 ms/s | 16 ms/s |

- **Several rectangles** (`ui.dlist`, `paint_damage_rects`, `paint.c` "Several damaged
  rectangles"): `damage_add` keeps the changes as up to 16 non-overlapping rectangles besides
  the box around them; `paint_damage` joins pairs whose union wastes less than `DAMAGE_PASS_PX`
  (1,024 px, measured: 256 to 4,096 px all cost within 10 %), falls back to one box when they
  cover 70 % of it, and `paint_out` paints each one (clear, `paint_root` clipped to it). Only on
  the CPU path and without a window being dragged; `LORD_DAMAGE=1` forces one box.
- **Blurred surfaces** keep the 0.8.1 rule ("Paint: repaint a blurred surface whole"): a rectangle
  that touches a blurred node or the margin its blur samples grows to the whole node, and every
  rectangle touching that margin joins it, so the margin is either repainted in the same pass or
  not at all this frame (`docs/LAYERS.md`).
- **Each pass is cheaper:** `paint_node` skips a node that clips its children (`overflow: hidden`)
  when it lies outside the clip (CPU path, not through transforms), and the opaque-cover search
  stops at such a node when it doesn't hold the region and keeps away from rounded corners.
- **Present:** the CPU path copies the painted rectangles (plus what the other buffer missed and
  the cursor) into the display's buffer instead of the box around them; Night Light tints the same
  rectangles.
- **The Monitor's side:** the dots are 9×9 images drawn once, and the map's frame has the card's
  own opaque color, so a repaint starts there instead of at the card.
- **What is left** of the animation's cost is mostly per frame and not per pixel: the tree walks of
  each pass (about 45 µs per rectangle), layout and damage walks, and the frame itself.
- **Tests:** `build/engine-test` `test_multi_damage`: 40 frames of 24 translucent round dots moving
  inside a clipped, rounded "map" under a translucent blurred bar (dots crossing its blur margin, a
  menu opening and closing), each frame painted rectangle by rectangle and compared with a full
  repaint: identical (at most 1 level, as `test_backdrop_band`); no rectangle outside the box or
  overlapping another; one box with `LORD_DAMAGE=1`; a full repaint set directly stays one box.
  With the blur rule turned off, 15 checks fail. `perf` was installed on VM 119 to find where the
  time went (`VM-SETUP.md`).

### Map

Time for an 800×560 frame on the map thread (`build/map-bench`):

| View | Average | Worst case |
|---|---|---|
| Whole world | 13.0 ms | 16.0 ms |
| Europe zoomed in | 6.7 ms | 8.1 ms |
| South America zoomed in | 4.4 ms | 5.2 ms |

It runs outside the frame. While it draws, the screen shows the previous frame shifted.

### Country by IP

- **First load:** the database (854 KB → 4.2 MB) is decompressed only once, on the sampling
  thread, outside the frame.
  - The whole `build/mmdb-test`, which includes that load and 400 broken files, takes 0.5 s in the
    VM.
- **Lookup:** each one walks at most 128 nodes.

## Screenshots (0.8.2, VM 119, English)

- An app's destinations with their switches, one blocked, and the 24-hour history:
  ![dark](monitor-network-082-dark.png) ![light](monitor-network-082-light.png)
- Settings → Privacy → Monitor with "Keep a traffic history" and "Clear History":
  ![privacy](monitor-network-082-privacy.png)

## Screenshots (0.8.1, process actions, VM 118)

- The Stop and Get Info buttons with a row selected: ![light](monitor-process-toolbar-light.png)
  ![dark](monitor-process-toolbar-dark.png)
- The right-click menu: ![light](monitor-process-menu-light.png) ![dark](monitor-process-menu-dark.png)
- The stop sheet: ![light](monitor-process-stop-light.png) ![dark](monitor-process-stop-dark.png)
- Get Info of a download (curl), Memory, Statistics and Open files and ports:
  ![memory](monitor-process-info-memory-light.png) ![statistics](monitor-process-info-stats-light.png)
  ![files](monitor-process-info-files-light.png); dark: ![memory](monitor-process-info-memory-dark.png)
  ![statistics](monitor-process-info-stats-dark.png) ![files](monitor-process-info-files-dark.png)
- A root process as an administrator (read through `proc-info`): ![root](monitor-process-info-root.png)
- A non-administrator trying to stop another user's process: ![refused](monitor-process-needadmin.png)
- Right to left (Arabic): ![rtl](monitor-process-rtl.png)

## Screenshots (0.8.1, VM 118, real traffic: a browser on a video site over QUIC and five downloads)

- Network, dark and light: ![dark](monitor-network-dark.png) ![light](monitor-network-light.png)
- The browser chosen (detail panel, its own chart): ![dark](monitor-network-detail-dark.png)
  ![light](monitor-network-detail-light.png)
- The map with traffic moving (a still frame): ![map](monitor-network-map-flow.png); with Reduce
  motion: ![still](monitor-network-map-still.png)
- Minimum window size in Spanish (the longest values go on their own line):
  ![es](monitor-network-min-es.png); right to left (Arabic): ![rtl](monitor-network-rtl.png)
- Before the fix (0.8.0, Spanish): a 400 KB/s HTTP/3 download shows "—" and "0 B":
  ![before](monitor-network-before.png)

## Screenshots (VM 123)

- Overview: ![Overview](monitor-overview.png)
- CPU: ![CPU](monitor-cpu.png)
- Memory: ![Memory](monitor-memory.png)
- Disks: ![Disks](monitor-disks.png)
- Network, with curl expanded: ![Network](monitor-network.png)
- Firefox chosen: ![Chosen app](monitor-network-app.png)
- Map:
  - the world: ![Map](monitor-map-world.png)
  - zoomed in on Europe: ![Europe](monitor-map-europe.png)
  - zoomed in on South America: ![South America](monitor-map-south-america.png)
  - Germany chosen: ![Chosen place](monitor-map-selection.png)
- Settings → Privacy → Monitor: ![Privacy](monitor-privacy.png)
- In Arabic (RTL): ![RTL](monitor-rtl.png)

## Pending

- **Privacy link:** with the window's default size (1180×700 on 1280×800), the "Monitor privacy
  settings" link in the map footer falls in the Dock area, which takes the click. It can still be
  reached through Settings → Privacy.
- **Arabic and other languages:** the Monitor's texts exist in es/en/pt. In Arabic the window
  mirrors correctly, but the texts show in English until the language pack translates them.
- **Sensors:** the VM has no CPU `hwmon` or fans, so temperature and fan show "—". Still needs
  testing on real hardware (the code looks for `coretemp`, `k10temp`, `zenpower`, `cpu_thermal`
  and `pwmfan`).
- **Phase 2:** what's in the "Phase 2" section.

## Attributions

- **Countries by IP:** DB-IP Lite Country © db-ip.com, CC BY 4.0 license
  (<https://creativecommons.org/licenses/by/4.0/>).
  - The attribution is shown in About LordOS, at the foot of the map and in
    `/usr/share/doc/lordos/copyright`.
  - Source used: `dbip-country-lite-2026-10`.
- **Country outlines and names:** Natural Earth (<https://www.naturalearthdata.com>), public
  domain.
