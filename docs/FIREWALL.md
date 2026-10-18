# Per-app firewall

The owner of the machine decides which of their apps may use the network, just like the "network
access" permission on Android and other systems. It is built in steps:

| Step | What | Status |
|---|---|---|
| 1 | Rule store, the decision maker and the names learned from DNS (`server/src/fw`), with tests | **done** |
| 2 | Enforce it in the kernel with cgroup hooks: the root service `lord-netperm` (`server/src/netperm`) | **done** (VM 122) |
| 3 | Monitor: the "Rule" column and the counters | **done**: per-app switch and counters; no per-destination switch |
| 4 | Settings: "Trusted apps" and "Restricted apps" | **done**: lists and mode (silent / off) |
| 5 | "Ask" mode | planned (see "The ask mode") |

Step 1 is C code only, with no daemon and without touching the kernel or the network (step 2,
below, is the service that enforces it):

```
server/src/fw/rules.{c,h}    rules, lists, mode, fw_decide, JSON
server/src/fw/dnsmap.{c,h}   IP → names per app (LRU with TTL) and DNS response reader
server/src/fw/fwmap.{c,h}    internal hash table (per-process seed)
tests/fw_rules_test.c        build/fw-rules-test
tests/dnsmap_test.c          build/dnsmap-test
```

Both tests are built and run with `make -C server test`.

## Rules

### A rule

| Field | Values |
|---|---|
| app | canonical id of the app (see below) |
| direction | `out` (leaves the app) or `in` (comes into one of the app's ports) |
| destination | `any`; an exact domain (`ejemplo.com`) or a wildcard (`*.ejemplo.com`); an IPv4/IPv6 address or network (`192.0.2.7`, `10.0.0.0/8`, `2001:db8::/32`); `lan`; `internet` |
| ports | one (`443`), a range (`8000-8080`) or any |
| protocol | `tcp`, `udp` or any |
| action | `allow` or `deny` |
| duration | forever; until the app quits; just once |

Destination details:

- **Wildcard:** `*.ejemplo.com` covers `ejemplo.com` and everything ending in `.ejemplo.com`, at
  any depth. It doesn't cover `otroejemplo.com`.
- **Domains:** stored in lowercase and without the trailing dot. Labels of 1 to 63 characters from
  `[a-z0-9-_]` and up to 253 in total. The last label can't be all digits, because that is a
  mistyped IP. International names go in punycode (`xn--...`).
- **Networks:** the bits outside the prefix must be 0. For example, `10.0.0.1/8` is rejected.
  Also, `::ffff:a.b.c.d/n` (with n ≥ 96) is stored as its IPv4 network.
- **LAN** (`fw_addr_is_lan`):
  - IPv4: 0/8, 10/8, 100.64/10 (CGNAT, as in the Monitor), 127/8, 169.254/16, 172.16/12,
    192.168/16, 224/4 (multicast) and 255.255.255.255;
  - IPv6: `::`, `::1`, `fc00::/7` (ULA), `fe80::/10` and `ff00::/8`;
  - an IPv4-mapped IPv6 address counts as its IPv4.

  **Internet** is everything that isn't LAN.

- **Duration:** only "forever" rules are saved to the file.
  - "Until it quits": removed by `fw_app_quit(app)`.
  - "Once": `fw_decide` removes it as soon as it decides a connection.

### An app's id

`fw_app_canon` accepts:

- `lordos` (lord-server and its built-in apps);
- `desktop:<id>` (Linux apps with a `.desktop`);
- `lpk:<id>` (`.lpk` apps);
- the absolute path of the executable (what the Monitor uses when it can't find a `.desktop`).

cgroup scopes are translated automatically: `app-lordos-lpk-<id>.scope` → `lpk:<id>` and
`app-lordos-<id>.scope` → `desktop:<id>` (see `server/src/sec/cgroup.h`). This way the kernel hook,
which sees the scope, and the Monitor, which sees the `.desktop`, talk about the same app.

A valid id:

- has up to 127 bytes of `[A-Za-z0-9._:@+~-]`;
- may contain `/` only in the path form, which doesn't allow empty segments, `.` or `..`;
- can't be `desktop:` or `lpk:` without the id.

### Lists

- **Trusted apps:** nothing is recorded and nothing is asked for them. Explicit rules still
  apply: a `deny` the owner set for a trusted app is enforced.
- **Restricted apps:** each one has a set of restrictions. The four network ones are decided by
  `fw_decide` and enforced in the kernel by `lord-netperm` for every app with its own scope. The
  others are enforced where the resource is handed out (see
  [Non-network restrictions](#non-network-restrictions)).

  | Restriction | In the JSON | .lpk apps and Passwords (`lpk:<id>`) | Built-in apps (`lordos`) | Linux apps (`desktop:<id>`) |
  |---|---|---|---|---|
  | no internet | `no-internet` | `lord-netperm` | `lord-netperm` | `lord-netperm` |
  | no LAN | `no-lan` | same (also `127.0.0.1` and `::1`) | same | same |
  | no network | `no-network` | same | same | same |
  | no incoming connections | `no-incoming` | same | same | same |
  | no camera | `no-camera` | broker (`brokercam.c`) | `js_media.c` | not yet |
  | no microphone | `no-mic` | broker (`BOP_MIC`, camera videos) | `js_media.c` | not yet |
  | no screen capture | `no-screen` | broker (the `screen` permission; isolated apps have no capture API) | `js_media.c` | not yet |
  | no USB | `no-usb` | broker (`BOP_VAULT_FIDO`, the `usb` permission) | not offered (the desktop itself) | not yet |
  | no Bluetooth | `no-bluetooth` | broker (the `bluetooth` permission; no Bluetooth API in app mode) | not offered (the desktop itself) | not yet |
  | no access to your files | `no-files` | broker (file picker, camera saves, the `files` permission) | not offered (the desktop itself) | not yet |
  | no reading the clipboard | `no-clipboard` | broker (`BOP_CLIP_GET`, the `clipboard` permission) | not offered (the desktop itself) | not yet |
  | no notifications | `no-notifications` | `sdk/notify.js` and the `notifications` permission | `sdk/notify.js` | not yet |

An app is in only one list: trusting it removes it from the restricted ones, and restricting it
removes the trust. Non-network restrictions are queried with `fw_blocks(app, flag)`, through
`server/src/sec/restrict.h`. They beat a per-app permission granted in `permisos.json`: a
restricted app with no camera doesn't get it even if it was given once.

### Non-network restrictions

`lord.fw` (`ui/js_fw.c`) owns the rule store; `restrict_set_store()` hands the same store to
`sec/restrict.c`, so a change made in Settings applies to the next request at once, without
reloading anything. What each kind of app goes through:

- **Isolated apps (`.lpk` and Passwords, firewall id `lpk:<id>`).** Everything they get from the
  user goes through the broker, so the broker is the enforcement point (`brokersrv.c`,
  `brokercam.c`). `restrict_perm_pkg()` returns `PERM_DENY` when a restriction covers a
  permission, whatever `permisos.json` says. A restricted request is refused without drawing a
  prompt, and the reply carries a code line (`fw.restricted.camera app=<id>`), which the app side
  throws as a translated error (`errors.json`):
  - `BOP_PERM` answers `"0 fw.restricted.<perm> …"` for camera, microphone, screen, USB,
    Bluetooth, files, clipboard and notifications;
  - `BOP_CAMERA`: `list` returns no cameras, `open` is refused, and photo or video saves (they are
    user files) are refused under `no-files`;
  - `BOP_MIC`, `BOP_PICK` (the file picker), `BOP_CLIP_GET` (reading the clipboard) and
    `BOP_VAULT_FIDO` (the USB security key of Passwords) are refused;
  - `BOP_DEVICE` does not light the indicator for a restricted device;
  - a prompt the user allows, or a file the user picks, after the app was restricted is refused too.
- **Built-in apps inside `lord-server` (firewall id `lordos`).** They share one JS context with
  the desktop shell, so the checks are at the native boundary that knows what is asked for:
  `js_media.c` `permitted()` refuses camera, microphone and screen capture and recording with
  `fw.restricted.*`, before the permission. `sdk/notify.js` drops notifications from built-in and
  isolated apps (`lord.fw.blocks()`); Settings' own system warnings (battery) are exempt. The
  `LordOS` row in Settings does not offer files, USB, clipboard or Bluetooth: that row is the
  desktop itself (Files, copy and paste, Disks, Settings → Bluetooth), and the screen says so in
  one line. Restrict individual apps instead.
- **Linux apps (`desktop:<id>`) and terminal programs.** They are not sandboxed and reach devices
  directly, so today only the network restrictions apply to them. Kernel-level checks for them
  are separate work.

**Live revocation.** Adding a restriction to a running app (`restrict_changed()`, called by
`setRestricted`) takes the device away at once, the same way revoking the permission does: for an
isolated app `brokersrv_revoke()` sends `revoke`, kills the camera's `lord-media`, closes the
microphone stream and stops a camera video that records sound; for the built-in apps a hook in
`js_media.c` kills the camera helper (the window gets `closed` with `revoked`) and finishes
microphone and screen recordings properly. New notifications and file picks are refused from that
moment.

Tested by `server/tests/broker_restrict_test.c` (`make -C server test`): the real broker code
against a fake app, every restriction with and without it, no prompt, the error codes, live
revocation of camera and microphone and the built-in apps' hook.

### Precedence

`fw_decide` follows this order. The first one that applies decides:

1. **The app's restrictions.** They always deny, before any rule. They are checked in this order:
   `no-network`, `no-incoming` (incoming only), `no-internet` (a connection with no known address
   counts as internet) and `no-lan`.
2. **Explicit rules.** Of all the app's rules that match in direction, protocol and port, the one
   with the **most specific destination** wins:

   | Destination | Specificity |
   |---|---|
   | exact domain | the highest |
   | wildcard | more labels, more specific: `*.cdn.ejemplo.com` > `*.ejemplo.com` |
   | address or network | longer prefix, more specific: `/32` > `/24` > `/8` > `/0` |
   | `lan` / `internet` | lower than any network |
   | `any` | the lowest |

   Ties are broken in this order:
   1. the smallest port range (a concrete port beats "any");
   2. a concrete protocol beats "any";
   3. **deny beats allow**;
   4. the newest rule.
3. **Trusted app.** Allowed without recording (`record = 0`) and without asking.
4. **Mode** (`fw_set_mode`, global for the user):

   | Mode | Result |
   |---|---|
   | silent (default) | allowed and recorded (`record = 1`) so the owner can review it in the Monitor |
   | ask | `FW_V_ASK` (pending: the owner must be asked) |
   | off | allowed without recording |

   The mode only changes this last step: rules and restrictions still apply with the mode off.

"Deny > allow" holds between equally specific rules. Between different rules, specificity rules.
That's why the typical "deny everything except api.ejemplo.com" case works:

| Rule | Connection to `api.ejemplo.com` | Connection to `www.ejemplo.com` |
|---|---|---|
| `deny any` | matches (minimum specificity) | **decides: denied** |
| `deny *.ejemplo.com` | matches (wildcard) | matches |
| `allow api.ejemplo.com` | **decides: allowed** (exact) | — |

For `www.ejemplo.com`, `deny *.ejemplo.com` and `deny any` match, and the wildcard wins for being
more specific. The result is the same: denied.

- **Two rules with the same destination:** if they match in app, direction, destination, ports and
  protocol, they are the same rule. Adding it again replaces the previous decision and keeps the
  id. That's why the Monitor's switch can go back and forth without piling up rules.
- **Several names for one IP:** when an IP has several names (a shared CDN), all of them are
  evaluated. The most specific wins and, on a tie, deny.

`FwDecision` returns:

- `verdict`: allow, deny or ask;
- `reason`: which step decided (restriction, rule, trusted or mode);
- `rule_id`: the rule that decided;
- `flag`: the restriction that denied;
- `record`: whether it must be recorded.

### Structures and cost

Each app has its own index. Finding the app is a hash table lookup (O(1)), and inside:

| Destination | Structure | Cost of a lookup |
|---|---|---|
| exact domain | hash table name → rules | O(1) per name |
| wildcard | hash table suffix → rules, walking the name's labels right to left (`a.b.ejemplo.com`, `b.ejemplo.com`, `ejemplo.com`, `com`) | O(labels), at most 127 |
| address or network | binary prefix trie per family, with the rules on each node | at most 33 nodes (IPv4) or 129 (IPv6) |
| `lan`, `internet`, `any` | one list per destination | O(rules of that destination) |

- **Per-destination lists:** each list only holds the rules for that exact destination, which
  differ in direction, ports or protocol. They are short lists.
- **Rule ids:** kept in another hash table (`fw_rule_get` and `fw_rule_remove` in O(1)).
- **On removal:** trie nodes and empty keys are pruned, and the app is released when it has
  nothing left.
- **Hash:** FNV-1a with a per-process seed and MurmurHash3's final mix. Keys can't be predicted
  from outside.
- **Caps:** 65,536 rules, 4096 apps with state and a 4 MB file.

In the test, with 50,000 rules from 500 apps, a decision with one name and one IPv4 takes
0.17 µs on the development machine (clang -O2) and 0.21 µs on Linux (gcc 14 -O2).

### File

There is one per user: `~/.config/lordos/firewall.json`. It is compact JSON read by the strict
reader in `server/src/sec/json.c`. For example:

```json
{"v":1,"mode":"ask","trusted":["lordos"],"restricted":{"lpk:juego":["no-internet","no-camera"]},
 "rules":[{"app":"desktop:org.chromium.Chromium","dir":"out","dst":"*.doubleclick.net","action":"deny","when":1760000000},
          {"app":"lpk:juego","dir":"in","dst":"lan","port":"27015","proto":"udp","action":"allow","when":0}]}
```

- **On save** (`fw_save`):
  - the write is atomic: a `.tmp` with mode 0600 and `O_NOFOLLOW`, `fsync` and `rename`;
  - apps and rules go in a fixed order, so the same content gives the same file;
  - only "forever" rules are written.
- **On read** (`fw_load` / `fw_from_json`) everything is validated, and any error rejects the
  whole file:
  - unknown or repeated keys;
  - `v` other than 1;
  - invalid ids, destinations, ports (1–65535, `lo ≤ hi`, no leading zeros), protocols or
    actions;
  - unknown or repeated restrictions;
  - an app that is in both lists at once;
  - ids not in their canonical form;
  - 0 bytes and files that aren't regular or exceed 4 MB.

  The store only changes if everything validated: a new one is built and swapped in. If the file
  doesn't exist, `fw_load` returns 1 and the store stays as it was.

The path is on the `lord.fs` deny list (`server/src/fsops.c`, like `settings.json` and
`permisos.json`, see `AGENTS.md`): a LordOS app can't write its own rules. A Linux app runs as the
user and could edit the file, just like `permisos.json`; `.lpk` apps can't, because of Landlock.

### Names for an address (dnsmap)

A connection only carries an IP. To apply a domain rule, you need to know which name that app
resolved for that IP. `dnsmap` learns it from the DNS responses the system resolver already
received, without asking anyone anything: no reverse DNS, no services.

- **Per app:** the key is (app, IP) → names. What one app resolved is of no use to another, so an
  app can't "lend" an allowed name to an IP for another app.
- **CNAME chains:** for `www.ejemplo.com → CNAME edge.cdn.net → CNAME a1.cdn.net → A
  203.0.113.10`, the IP gets all three names. The TTL is the smallest in the chain.
- **Which records are used:**
  - only A/AAAA records whose owner is on the chain that starts at the question (a stray record
    is ignored);
  - looping chains are cut;
  - the chain has at most 16 hops.
- **Table:**
  - it is a bounded LRU (4096 pairs by default) with 8 names per IP;
  - TTLs are clamped to [60 s, 1 day];
  - a lookup marks those names as the most recent;
  - `dnsmap_lookup` returns the current names from newest to oldest, ready for
    `FwQuery.hosts`.
- **Reader** (`dns_parse_response`):
  - reads only the answer section, class IN, types A, AAAA and CNAME;
  - requires `QR = 1`, `OPCODE = 0` and a single question;
  - checks every access: labels up to 63, names up to 255 bytes, `rdlen` inside the packet, A of
    4 bytes, AAAA of 16 and a CNAME that fills exactly its `rdata`;
  - compression pointers can only point back before the stretch being read, and each jump lowers
    that limit, so no loops are possible; there is also a cap of 64 jumps;
  - label types 0x40 and 0x80 are rejected;
  - a name that isn't a host name (spaces, odd bytes) invalidates the record, not the packet.

**Rule for step 2:** `dnsmap_learn` is only fed responses that the system resolver received
(`systemd-resolved` or LordOS's own), never packets the app received on its own sockets.
Otherwise, an app with its own DNS server could claim that `permitido.com` is any IP and escape a
"allow only permitido.com" rule.

### Tests

`build/fw-rules-test` covers:

- canonical ids and scopes;
- valid and invalid destinations, round trip;
- LAN and internet;
- every precedence step;
- domain versus wildcard, nested v4/v6 networks and the mapped IPv4;
- ports and protocols, and deny versus allow at the same level;
- the durations;
- every restriction;
- JSON round trip (identical text), through a file, and 40 bad inputs;
- 20,000 random mutations of the JSON: it never crashes, and what it accepts it writes back the
  same;
- 30,000 random operations (add, remove, decide), compared against an obvious linear version of
  the decision maker: 0 differences;
- a benchmark with 50,000 rules.

`build/dnsmap-test` covers:

- good responses, truncated at every byte, self-pointing and forward pointers, names longer than
  255 bytes, wrong `rdlen`, other classes and odd names;
- the CNAME chain, stray records, the CNAME loop, expiry and the TTL caps;
- the per-IP cap, the global LRU and separate apps;
- use with the rules (`*.doubleclick.net` behind a CNAME);
- 200,000 random or mutated packets, each one in its own heap block.

During development they were also run with `-fsanitize=address,undefined`, with clang on the development machine and
with gcc 14 on Linux.

## Step 2: `lord-netperm`, the rules in the kernel

```
server/src/netperm/np_shared.h   map types and the decision (np_decide), identical in BPF and C
server/src/netperm/np.bpf.c      the 7 cgroup BPF programs (restricted C, clang -target bpf)
server/src/netperm/bpf2c.c       build tool: clang's .o → build/gen/np_bpf.h
server/src/netperm/bpfsys.{c,h}  direct bpf() syscall: maps, load, bpf_link, ring
server/src/netperm/npcompile.{c,h}  rules → map entries (pure C, with a test)
server/src/netperm/npdns.{c,h}   names from the system resolver
server/src/netperm/npproto.h     the socket between lord-server and lord-netperm
server/src/netperm/netperm.c     the service
server/src/ui/js_fw.c            lord.fw (lord-server)
tests/npcompile_test.c           build/npcompile-test (in make test)
tests/netperm_integration.sh     test against the kernel (on the VM)
tests/netperm_bench.c, netperm_measure.sh   measurements
```

### The service

`lord-netperm` is a separate root process, with its own systemd unit (`lord-netperm.service`,
`Restart=always`, before `lordos.service`). It isn't part of `lord-netd` or `lord-sessiond`.

- **No arguments, no execution.** It doesn't launch programs. With arguments, it doesn't start.
- **No network of its own.** It creates no AF_INET or AF_INET6 sockets. The unit enforces this with
  `RestrictAddressFamilies=AF_UNIX AF_PACKET` and leaves it only `CAP_BPF CAP_NET_ADMIN CAP_PERFMON
  CAP_DAC_READ_SEARCH CAP_NET_RAW`. It has `ProtectSystem=strict`, `ProtectHome=read-only` and the
  rest listed in the unit (`VM-SETUP.md`, step 20).
- **The programs are inside the binary.** `clang -target bpf` compiles `np.bpf.c` and `build/bpf2c`
  turns it into a C header with the instructions and the map relocations, by name. `bpf2c` rejects
  everything the loader can't resolve: non-inlined functions, global data and relocations that
  aren't maps. No ELF file is read at run time. Neither libbpf nor bcc is used. Without clang
  (`BPF=0`) it still builds, and the service reports "BPF not available" and does nothing.
- **Where it attaches.** To the cgroup of `lordos.service`, with `bpf_link`, which contains the
  subtree of every session. A single attachment covers all users: each scope has its own id.
  - systemd deletes and recreates that cgroup every time `lordos.service` restarts. That's why the
    service watches it with inotify on `system.slice` and once per second, and re-attaches if the
    id changed.
  - If the service stops, the links are closed and everything is allowed. The desktop doesn't
    depend on it.
  - **Also on `user@<uid>.service`** (closing the `systemd-run --user` escape, see
    [Escape via `systemd-run --user`](#escape-via-systemd-run---user-the-user-union)): the same
    programs are attached a second time, with a `bpf_link` per session, to each LordOS session
    user's `user@<uid>.service` cgroup. Re-attached the same way when that cgroup changes (the user
    manager restarting) or disappears.
- **The apps.** It reads the session folders (`lordos-session-<uid>`) with inotify, plus a rescan
  every 5 s:
  - `server` is `lordos`;
  - `app-lordos-<id>.scope` is `desktop:<id>`;
  - `app-lordos-lpk-<id>.scope` is `lpk:<id>`.

  The cgroup id comes from `name_to_handle_at`, which is the same thing the kernel sees.

### The programs

| Hook | What it checks |
|---|---|
| `cgroup/connect4`, `connect6` | TCP and connected UDP. IPv4-mapped IPv6 addresses count as IPv4 |
| `cgroup/sendmsg4`, `sendmsg6` | unconnected UDP (`sendto` with an address) |
| `cgroup/bind4`, `bind6` | incoming: "no incoming connections" and `in` rules. A bind to port 0 (ephemeral, client) isn't checked |
| `cgroup/sock_create` | "no network": the app can't even create an AF_INET/AF_INET6 socket, so it can't ping either |

Each program does the following:

1. It finds the app by its cgroup's ancestor at the scope level
   (`bpf_get_current_ancestor_cgroup_id`). This way, a cgroup the app creates inside its scope is
   still the same app.
2. It decides with `np_decide` (`np_shared.h`).
3. It returns 1, or 0. With 0 the app gets `EPERM` ("Operation not permitted").

Anything that isn't a session app passes right away: `lord-sessiond`, the login screen and the rest
of the system.

#### Escape via `systemd-run --user` (the `user@` union)

Root-owned scopes (see `docs/PRIVACY.md`) stop an app from writing `cgroup.procs` to leave its
scope, but an unsandboxed Linux app can still ask its own systemd **user manager** for a new unit:
`systemd-run --user --scope <cmd>` (a scope) or `systemd-run --user <cmd>` (a transient service).
The user manager has PID 1 do the move (`AttachProcessesToUnit`), which only checks the UID, not
the source cgroup, so the process lands in `user@<uid>.service/app.slice/…` — outside
`lordos.service`, where the programs above are not attached. A "no internet" app could reach the
internet from there.

**Chosen fix (option a): attach to `user@<uid>.service` + a per-user restriction union.** For each
LordOS session user, `lord-netperm` attaches the same cgroup programs a second time to that user's
`user@<uid>.service`. There the process is not in any app scope, so `find_app` (`np.bpf.c`) falls
back to `np_union`, keyed by the `user@<uid>` cgroup id (the ancestor at `cfg->user_level`). Its
value is the **strictest union** of the restrictions (the `NP_R_*` bits ORed) of all of that user's
currently-running restricted apps, with `trusted=1` so anything the union does not forbid is
allowed silently. If the user has no restricted app running, there is no entry and `user@` is fully
open (the user manager and its own services are untouched). `lord-netperm` rebuilds the union
whenever a scope or a rule changes (`sync_union`), and removes it when the user's session or user
manager goes away.

Why this and not the alternatives:

- A process escaping through a `systemd-run --user` **service** is forked by the user manager, not
  by the app, so its process lineage no longer reaches the restricted app. Carrying the app
  identity in BPF task-local storage and inheriting it across `fork` (option c) therefore cannot
  cover the service case — and it would also need a `BPF_PROG_TYPE_TRACING`/raw-tracepoint program
  plus a BTF-typed task-storage map, which our own `bpf()` loader (`bpfsys.c`) does not build.
  Kernel 6.12 has BTF and `bpf` LSM, but the loader would have to grow that machinery.
- Denying the app access to the user bus / `$XDG_RUNTIME_DIR/systemd/private` (option b) can't be
  enforced for unsandboxed Linux apps (Landlock is not applied to them).
- The union is **restriction-only**: it never grants access, so an escaped process is always at
  least as restricted as its own policy. The cost is that it is coarse — while a restricted app is
  running, every process under that user's `user@` is held to the union. That is acceptable because
  LordOS's own apps live under `lordos.service` (unaffected), and it activates only while a
  restricted app runs. A precise per-process identity (option c) is a possible future refinement if
  the loader gains tracing support.

`sock_create` ("no network") is **not** enforced through the union, so the user manager can still
create its own sockets; a "no network" escapee can open a socket but every `connect`/`sendmsg`/`bind`
is denied by the union all the same. The decision helper (`np_decide`) is unchanged, so
`tests/npcompile_test.c` still covers it.

**Maps**

| Map | Type | Key → value |
|---|---|---|
| `np_cfg` | array | scope level, enabled, `user@<uid>` level |
| `np_apps` | hash | cgroup id → restrictions, trusted, mode and generic list (lan / internet / any) |
| `np_union` | hash | `user@<uid>.service` id → strict union of the user's restricted apps |
| `np_lpm4`, `np_lpm6` | LPM trie | (app index, network) → ordered list of rules |
| `np_seen` | LRU | (cgroup, address, port, protocol, connection direction, verdict) → last time reported |
| `np_counts` | hash | cgroup id → allowed, denied |
| `np_events` | ring buffer | new connections to the service |

**What is counted and reported.** Every TCP `connect()` and every `bind()` is counted. A UDP
`sendto` and a UDP `connect()` count only the first time in 5 s, so the numbers don't get
inflated: getaddrinfo does a UDP connect for each address to sort them (RFC 6724).

The service gets one notice per new connection, through the ring, only if that same connection
wasn't reported in the last 5 s. Nothing is per packet: no hook runs per packet and no packet is
processed in user space. The exception is the DNS responses of the source without resolved, below.

### From rules to maps (`npcompile.c`)

There's no time in the kernel for the full precedence of `fw_decide`, so it is precomputed:

1. **Restrictions, trusted and mode** go in `np_apps`. "Ask" is compiled as silent (see "The ask
   mode"). "Just once" rules are not compiled.
2. **`lan`, `internet` and `any`** go in the app's generic list, in precedence order.
   - Up to 16 rules: `NP_GEN_MAX`.
   - They are the least specific, so they go last.
3. **One network per rule.** Each distinct network is a trie node. Its list carries its own rules
   and those of every network that contains it, sorted as in `fw_decide`:
   1. most specific destination;
   2. smallest port range;
   3. concrete protocol;
   4. deny;
   5. the newest.

   The trie's longest match is enough: the first entry that matches in direction, protocol and
   port wins. Up to 16 entries per node (`NP_LIST_MAX`). Any extra ones are reported in the log.
4. **Domains.** For each address the resolver associated with a name that matches one of the
   app's domain rules, there is a /32 or /128 node.
   - Its list starts with those domain rules, which are the most specific.
   - The networks that contain it follow.
   - With several names for one IP, the most specific wins and, on a tie, deny, just like in
     `fw_decide`.

With each new DNS response, only the nodes for those addresses are built, for the apps that have
domain rules. Every 30 s, and with every rule change, the whole app is recompiled and whatever
expired or is left over is removed (the installed map is compared with the new one).

**The test** (`build/npcompile-test`) runs the kernel's same `np_decide` with a simulated trie and
compares it against `fw_decide`:

- 3000 random stores, with domains, wildcards, nested v4/v6 networks, lan/internet/any, ports,
  protocols, incoming, trusted, restricted and the three modes;
- 480,000 connections, with learned names, binds and mapped IPv4: **0 differences** in verdict,
  reason and rule;
- two deliberate mutations of the compiler: inverting "deny wins" and dropping prefix length from
  the ordering. Both produce differences, so the test catches them;
- also, readable cases, the trie keys, the list cap and the two DNS sources (below);
- 401 rules with 4000 learned addresses compile into 2200 nodes in 21 ms on the VM.

### Where the names come from

Only from responses the system resolver gave (`npdns.c`):

1. **With systemd-resolved** (Debian 13 and the ISO): the monitoring interface over varlink,
   `/run/systemd/resolve/io.systemd.Resolve.Monitor`, with `SubscribeQueryResults`. Only root can
   subscribe.
   - Every response resolved gave to any program arrives: via NSS (`nss-resolve`), via the
     127.0.0.53 stub and via D-Bus.
   - It carries the records in wire format (`raw`). From them a DNS message is built that goes
     through `dnsmap`'s strict reader (`dns_parse_response`).
   - **Why it's enough:** an app can't write into resolved's cache. If it uses its own DNS server or
     DNS over HTTPS, those responses don't arrive here. With an "only permitido.com" rule, an IP the
     app got from elsewhere isn't in the trie and is denied.
   - If a lying server slips in a record that doesn't follow from the question (`evil.example` → an
     A of another name), `dnsmap` ignores it. There is a test.
2. **Without resolved:** `NETWORK.md` says LordOS writes `/etc/resolv.conf` (as root). The UDP
   responses that arrive **from port 53 of those servers** are taken.
   - They are read by an AF_PACKET socket that only receives. A classic BPF filter lets through
     only unfragmented UDP with source port 53.
   - The servers come from `resolv.conf`, which is reread every 30 s.
   - It's the simplest thing that is still trustworthy: what an app asks another server isn't
     learned, and an app without root can't send a packet "from port 53" of the server.
   - There is a test of the reader: another server, another port, fragments, lying lengths and
     truncation.

The names belong to the system, not to each app: `dnsmap` stores them under a single key. resolved
doesn't say which program asked. It isn't a hole, because they are real responses: an app can
connect to the IPs of `permitido.com` even if another one requested them.

**The race.** The trie node has to be in place before the app connects.

- On the VM, resolved notifies the monitor between 150 and 300 µs **before** getaddrinfo returns
  in the app.
- A freshly launched `curl -4 https://example.com`, with "deny everything except example.com",
  worked **20 out of 20 times with resolved's cache empty** and 20 out of 20 with a warm cache.
- If it ever arrived late, that first connection is denied with EPERM and the next one works.

### The socket

`/run/lordos/netperm.sock` is 0666: anyone can connect.

- `lord-netperm` knows who each client is through `SO_PEERCRED`. There are up to 4 connections
  per user and each one sees only its user's apps.
- The protocol is in `npproto.h`: `u32` length, `u8` type and the data.
- **From lord-server to lord-netperm** there is a single message, with no data: `RELOAD`. Anything
  else drops the connection.
- **The rules don't travel over the socket.** `lord-netperm` reads the user's
  `~/.config/lordos/firewall.json` itself, with `CAP_DAC_READ_SEARCH`:
  - folder by folder with `openat`, `O_NOFOLLOW` and `O_NONBLOCK`;
  - each folder and the file must belong to the user;
  - the file must be a regular file of up to 4 MB;
  - it is validated by `fw_from_json`'s strict reader. If it doesn't validate, the previous rules
    stay.

  It reads it when the session appears and on every `RELOAD`.
- **From lord-netperm to lord-server:**
  - `STATUS`: `{bpf, dns, error}`;
  - `EVENT`: `{app, v, dir, proto, addr, port, by, rule, flag, record}`, a new connection;
  - `COUNTS`: `{app: [allowed, denied]}`, every second, only if it changed.

  If nobody reads, notices are dropped. Counters are sent again when they change.

### `lord.fw` (lord-server, `js_fw.c`)

lord-server owns the user's rules: it loads them at startup, changes them and saves them with
`fw_save`. Then it sends `RELOAD`. The socket never blocks. It is read in `lord.fw.poll()`, which
the SDK calls once per second while the Monitor's Network tab is open.

| Call | What it does |
|---|---|
| `status()` | `{ daemon, bpf, dns, error }` |
| `state()` | `{ mode, trusted, restricted: { app: [restriction] } }` |
| `appRule(app)` / `setAppRule(app, 'allow'\|'deny'\|null)` | the switch's `{app, out, any}` rule |
| `setTrusted(app, on)`, `setRestricted(app, [restriction])`, `setMode('silent'\|'ask'\|'off')` | lists and mode |
| `rules(app)`, `addRule({ app, dir, dst, port, proto, action })`, `removeRule(id)` | rules (domains, networks, ports) |
| `poll()` | `{ counts, events }` |
| `blocks(app, restriction)` | `true` if the app is restricted in it (`sdk/notify.js`) |

Everything is validated in C:

- ids with `fw_app_canon`;
- destinations with `fw_parse_dest`;
- ports from 1 to 65535 (`lo ≤ hi`);
- protocols, actions and restriction names against their closed lists.

No call takes paths. Isolated `.lpk` apps don't have `lord.fw`.

In the SDK, `firewall` and `firewallState` from `sdk/monitor.js`, exported in `@lordos/sdk`.

**The Monitor** also attributes each process to the scope it runs in (`js_monitor.c`, `scope_of`,
which reads `/proc/<pid>/cgroup` with a cache). This way a `curl` inside `foot` is `foot`, just as
in the kernel. Before, it was the standalone `curl`, which can't be filtered.

### The modes

| Mode | In the kernel |
|---|---|
| silent (default) | whatever has no rule is allowed, and each new connection is counted and reported to the Monitor (`record`) |
| off | only explicit denials and restrictions apply; the rest is allowed without recording |
| ask | for now, the same as silent |

### The ask mode (next step)

A `connect` hook can't wait for the owner, and holding the connection in user space (like
NFQUEUE) is per-connection work outside the kernel. It will work as a "retry" prompt:

1. An app's first connection to a destination with no rule is **denied right away** (EPERM). The
   `ask` verdict goes in `np_apps`, and the notice goes out through the ring with `record`.
2. lord-netperm passes it to lord-server, and the SDK shows a system prompt (`PermissionPrompt`):
   ""App" wants to connect to ejemplo.com:443 — Allow once / Always / Deny".
3. The answer becomes a rule (once, until quit, or forever) and lord-netperm compiles it at once.
4. The app retries: browsers and most clients do it on their own, and the prompt offers "Retry"
   for the rest. The second connection gets through.

While the prompt is open, connections to that destination keep being denied without repeating
the prompt: `np_seen` already groups them for 5 s.

### Tested on VM 122

`tests/netperm_integration.sh` sets up an app scope like the launcher does and writes rules:
**18 out of 18**, with `lord-netperm` as a hardened systemd service.

| Case | Result |
|---|---|
| no rules | TCP to the gateway: works |
| deny the app | TCP `EPERM`, UDP `sendto` `EPERM`, TCP over IPv6 `EPERM` |
| allow it again | works |
| "deny everything except example.com" | `https://example.com` 200; `https://www.debian.org` `EPERM`; the IP the resolver gave works; an IP it didn't give, `EPERM` |
| "LAN only" (`no-internet`) | the gateway works; internet `EPERM` |
| `no-incoming` | `bind`+`listen` on 18080 `EPERM`; ephemeral bind and outgoing work |
| `no-network` | `socket()` `EPERM` |
| off | the explicit denial applies; the rest works |
| another process outside the scope | works |

**With the UI**, the "Network test" app is a `.desktop` that opens `foot` with a loop of `curl`
and `/dev/tcp`:

- **Monitor → Network:** the switch in the Rule column denies the app. `curl` and bash get
  "Operation not permitted", and the denied counter and the row's counter appear. Turning it back
  on, it works.
- **Settings → Privacy → App network:** with "no internet" checked in Restricted apps, the gateway
  works and the internet doesn't.

**`systemd-run --user` escape (the `user@` union).** With a restricted app running
(`desktop:aisla`, "no internet"), from a `foot` terminal placed in `app-lordos-aisla.scope`:

| Escape attempt | Result |
|---|---|
| `curl https://example.com` (direct, in the app scope) | blocked — "Could not connect" (existing scope rule) |
| `systemd-run --user --scope curl https://example.com` | blocked — the scope lands in `user@1000`, `np_union` denies the connect |
| `systemd-run --user curl https://example.com` (transient service) | blocked — same, even though the user manager forks it |
| `setsid curl https://example.com` | blocked — `setsid` does not change the cgroup, stays in the app scope |

`lord-netperm` logs each `user@` denial as `uid 1000 user@: denied … (union of
restricted apps no-internet)`. A normal process outside `user@`/`lordos.service` app scopes (e.g. an
SSH session scope) still reaches the internet while the restricted app runs, so normal apps are
unaffected. Cross-user: a restricted app for `bob` (uid 1001) blocks `bob`'s `systemd-run --user`
escape while `lord` (uid 1000, no restricted app) keeps full access — the union is per
`user@<uid>`, never leaking between users. The screenshot shows the four attempts blocked:

![systemd-run escape blocked](firewall-escape-blocked.png)

### Measurements (VM 122, 4 vCPU Xeon Silver 4108, kernel 6.12)

`tests/netperm_measure.sh`: 3 runs of 20,000 `connect()` to 127.0.0.1 inside a scope, and 3 GB
over TCP on 127.0.0.1.

| | TCP connect (median) | UDP connect (median) | throughput |
|---|---|---|---|
| lord-netperm off | 13.9 / 13.7 / 16.7 µs | 2.1 / 2.0 / 2.5 µs | 842 MB/s |
| on, no rules (silent) | 13.7 / 17.4 / 17.4 µs | 2.4 / 2.5 / 3.0 µs | 818 MB/s |
| on, 402 rules for the app | 13.7 / 13.8 / 17.7 µs | 3.1 / 2.6 / 2.5 µs | 829 MB/s |

- **The per-connection cost** shows in UDP, because its connect does no handshake: about
  **0.5 µs**. In TCP it stays within loopback noise.
- **Throughput doesn't change:** the differences are within run-to-run noise. No hook runs per
  packet.
- **The compositor:** lord-server's main thread, with the Monitor open on Network (which calls
  `lord.fw.poll` once per second), used 3.20 % of a core with lord-netperm on, 3.20 % off and
  3.13 % on again. 30 s each. Nothing of the firewall runs in the frame: `poll` reads a socket that
  doesn't block. Per-frame times weren't measured separately.

### Known limits

- **App scopes are root's now** (was: "Scopes belong to the user"). `lord-sessiond` creates the
  scopes and places each process; every `cgroup.procs` of the session subtree is root-owned, so a
  Linux app can no longer move itself to another scope, to `server` or to a new cgroup (tested on
  VM 122, see `PRIVACY.md`, «Root-owned app scopes»).
- **`systemd-run --user` escape: closed** (was the remaining hole). The programs are now attached to
  each user's `user@<uid>.service` too, and a process that escapes there is held to the per-user
  restriction union (see [Escape via `systemd-run --user`](#escape-via-systemd-run---user-the-user-union)).
  Tested on VM 122 (see below). What stays coarse: the union restricts the whole `user@` subtree
  while a restricted app runs, not just the escaped process. `firewall.json` and `permisos.json` are
  still writable by an unsandboxed app running as the user (a filesystem matter, not the firewall).
- **A freshly created scope** is registered via inotify within microseconds, before the program
  finishes starting (exec + libraries). If a connection arrived earlier, it would pass under the
  default rules.
- **What isn't checked:**
  - ICMP: denied only with "no network", through `sock_create`;
  - TCP `listen` without `bind` (ephemeral port);
  - incoming connections that arrive at an already open port.

  That would need `cgroup_skb/ingress`, which runs per packet.
- **Per-port rules with direction `in`** are applied at `bind`, with an unknown address.
- **The "ask" mode** is applied as silent.
- **Per-destination switches** in the Monitor: not yet. The API (`lord.fw.addRule`) exists.
- **Host names** still aren't shown in the Monitor. `lord-netperm` has them, but doesn't pass them
  to the session.

### Screenshots (VM 122)

- The denied app: `curl` and bash get "Operation not permitted".
  ![denied app](firewall-app-denied.png)
- Monitor → Network, with the app's switch on deny and the counters:
  ![monitor](firewall-monitor-denied.png)
- Monitor → Network with the app selected (rule and allowed and denied connections):
  ![detail](firewall-monitor-detail.png)
- Settings → Privacy → App network: mode, Trusted apps and Restricted apps:
  ![settings](firewall-settings.png)
- "LAN only" from Settings: no internet, the gateway yes. ![LAN only](firewall-lan-only.png)


## How the next steps plug in

What remains of steps 3 and 4 (the original design; what's done is above, in "`lord.fw`").

### Step 3: Monitor, the "Rule" column

- **The column:** `apps/monitor` already groups by `appKey` (`lordos`, `desktop:<id>` or the
  path), which is the same canonical id as here. Each app's switch writes the rule
  `{app, out, any, allow|deny, forever}`. Since adding the same destination replaces, going back
  and forth doesn't pile up rules.
- **Each expanded destination** can have its own switch: a rule with that domain (if `dnsmap`
  knows the name) or with that IP.
- **The counters:**
  - "allowed" and "denied" come from `FwDecision.verdict`;
  - those with `record = 1` are the ones silent mode leaves for review;
  - each connection carries the `verdict` (today fixed at `'allow'`, see `docs/MONITOR.md`,
    "Phase 2").

### Step 4: Settings

- **Where:** in Privacy (or in Network, with M7's grouping), two lists:
  - **Trusted apps:** `fw_set_trusted`;
  - **Restricted apps:** `fw_set_restricted`, with a checkbox per restriction from the table
    above.
- **The mode selector:** silent / ask / off.
- **Outside the network:** the non-network restrictions are queried with `fw_blocks` (through
  `sec/restrict.h`) before `perms.c`, by the broker for isolated apps and by `js_media.c` and
  `sdk/notify.js` for the built-in apps. Exactly what is enforced where is in
  [Non-network restrictions](#non-network-restrictions).
- **The JS API:** goes in its own `js_fw.c`, with input validation like the rest of `lord.*`. It
  never takes paths: the file's path is fixed by the server.
