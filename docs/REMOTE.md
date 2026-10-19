# Remote desktop on the LAN

Another computer on the local network opens `https://<machine IP>:5673` in a browser and logs in
with a user and password of the machine. Nothing needs to be installed: no extra programs, no
plugins, no third-party remote desktop protocol. It's part of `lord-sessiond`
([SESSIONS.md](SESSIONS.md)), the same process that handles the monitor's login screen.

The port is **5673**, which spells "LORD" on a phone keypad.

- **With the same user that's on the monitor:** you see and drive **the same session**. Both
  sides move the mouse and type at the same time. The monitor shows the notice
  «Remote desktop: lord · 192.168.x.x».
- **With another user:** they get **their own desktop**, running at the same time in the
  background.
- **If the session was already open:** it's resumed (including one somebody left on the monitor
  before switching users).
- **It's off by default.** It's turned on and off in Settings → Sharing → «Remote Desktop», as on
  popular desktop systems.
- **Terminal access** (SSH) is a separate switch on the same page: see
  [Remote login (SSH)](#remote-login-ssh).

![Settings → Sharing](remote-desktop-settings.png)

## Settings → Sharing

- The switch («Allow access from the network»), with the `remote-enable` and `remote-disable`
  actions.
- The URL built from the LAN IPv4 addresses and the certificate's SHA-256 fingerprint, to compare
  with the one the browser shows.
- **Who can connect:** all users, or only the `lordos-remote` group (`remote-config`;
  `remote-member` adds users to or removes them from the group).
- **Who can connect** and **Blocked addresses**: see [Blocked and allowed
  addresses](#blocked-and-allowed-addresses).
- **Connected now:** user, IP, whether they're watching this screen or their own desktop, and for
  how long, with a «Disconnect» button (`remote-kick`, only pids from lord-sessiond's list).

The actions change `/etc/lordos/remote/config` (`enabled=`, `port=`, `allow=`) and send SIGHUP to
lord-sessiond. Turning remote off cuts the connections, but the sessions stay open, as with RDP.

Without a screen, the same file is written by the Raspberry Pi image's `remote-desktop` switch file
(`RASPBERRY.md`) and by the Debian installer's `--remote-desktop` option for servers and VPS
(`INSTALL-DEBIAN.md`, "Servers and VPS").

## Login

![Web login](remote-desktop-web-login.png)

The page has the same look as the monitor's login screen: background, date, large clock, avatar
and rounded fields. It has a language selector (es, en, pt) and an error shakes the field.

**It doesn't show the user list**: who has an account isn't revealed to the network. The avatar
takes the initials of the name being typed.

- `POST /login` (only from the page itself: `Origin` must be `https://` + `Host`):
  - the server checks every field again, strictly: the user name `^[a-z_][a-z0-9_-]{0,31}$`, the
    password 1–1024 bytes without NUL, exactly the fields `user` and `pass`, once each
    (`server/src/remote/rsec.c`); the page checks the same before sending;
  - it's verified against `/etc/shadow` (`getspnam` + `crypt_r`, constant-time comparison);
  - only people's accounts: uid 1000–59999, a shell listed in `/etc/shells`, not locked, not
    expired, password not expired or due for a forced change, no `/etc/nologin`; **never root**
    (nor an account whose group is root's);
  - never an account without a password, not even the live ISO's;
  - a wrong user name, a refused account and a wrong password are answered exactly 1.25 s after
    the check began (unknown accounts spend the same `crypt` work on a dummy hash).
- **Lockout:** 3 failed sign-ins block the address (see [Blocked and allowed
  addresses](#blocked-and-allowed-addresses)). The old growing per-address waits stay as well.
- **Cookie** `__Host-lordos_remote`: 32 random bytes, `Secure; HttpOnly; SameSite=Strict; Path=/`,
  12 hours. Only its SHA-256 is kept, with a digest of the account's shadow hash: changing or
  locking the password ends it. Locking the screen ends every cookie of that user. Signing out is a
  `POST /logout` from the page.
- **Headers:** `Content-Security-Policy` with the SHA-256 of the page's own script and style (no
  `unsafe-inline`), `frame-ancestors 'none'`, `form-action 'self'`; `X-Frame-Options: DENY`,
  `X-Content-Type-Options: nosniff`, `Referrer-Policy: same-origin`, `Cache-Control: no-store`,
  `Permissions-Policy` (no camera, microphone, location, USB…), `Cross-Origin-Opener-Policy` and
  `-Resource-Policy: same-origin`. No HSTS: with a self-signed certificate it would make the
  browser's warning impossible to get past.

![Wrong password](remote-desktop-login-wrong.png)

## Blocked and allowed addresses

**Lockout** (`server/src/remote/lockout.c`):

- 3 failed sign-ins from one address, without a successful one in between (within 24 hours),
  block it for **15 minutes**; blocked again within 24 hours, **1 hour**; then **24 hours**.
- IPv6 addresses count per **/64** (a host has a whole /64 to hop through).
- The answer to the third failure is the sign-in page with the "blocked" message; after that the
  address is closed right after `accept()`, before any TLS work.
- Blocks are kept in `/var/lib/lordos/remote/blocklist` (root, 0600, folder 0700) and survive a
  restart of `lord-sessiond` or of the computer.
- A successful sign-in clears that address's count. **Accounts are never locked** (anybody could
  lock you out); repeated failures for one user name, from any address, only slow it down: none for
  the first 4, then 1, 2, 4 and at most 8 s before checking.
- Each failure and each block is logged to the journal with the address and the attempted user
  name, cleaned to printable ASCII. Never the password.
- The user on the monitor gets a notification ("Remote desktop: address blocked") with a button
  that opens Settings → Sharing.

![The notification on the desktop](remote-desktop-block-notice.png)

**Who can connect** (Settings → Sharing, `net=` in `/etc/lordos/remote/config`):

- **Local network** (the default): private ranges (10/8, 172.16/12, 192.168/16, 100.64/10),
  loopback, link-local and IPv6 unique-local, plus connections that arrive on a VPN interface of
  this computer (`wg-`, `ovpn-`, `ips-`, `oc-`, `fvpn-`, `ppp`, `tun`). Public addresses are
  closed.
- **Only these addresses**: only the networks in the list.
- **Any address**: everyone who can reach the port (still with the lockout).

**Manual blocks** win over everything. The lists live in `/etc/lordos/remote/access` (root, 0600):
`allow <cidr>` and `block <cidr>`, at most 64 of each. Networks are IPv4 /8–/32 or IPv6 /16–/128;
host bits are cleared.

![Settings → Sharing: blocked addresses](settings-sharing-blocked-light.png)

![Settings → Sharing: only these addresses (dark)](settings-sharing-allowlist-dark.png)

**Admin actions** (`remote_admin.c`, administrators only, validated on the root side by
`rs_cidr_parse`):

| Action                 | Argument            | What it does                                                    |
|------------------------|---------------------|-----------------------------------------------------------------|
| `remote-config`        | `net=lan\|list\|any` | the mode (with the existing `allow=`, `port=` lines)            |
| `remote-block`         | `<cidr>`            | adds a manual block to `access`                                 |
| `remote-unblock`       | `<cidr>`            | removes manual blocks inside it and appends `unblock <cidr>` to `/var/lib/lordos/remote/requests` (root 0600, `flock`); `lord-sessiond` lifts the lockout blocks inside it on SIGHUP |
| `remote-allow`         | `add\|del <cidr>`    | the "only these addresses" list                                 |

Unblocking from Settings forgets that address's history (its next block is 15 minutes again).

## Browser protocol (WebSocket `/ws`)

**Server → browser.** Binary:

- `[1][x,y,w,h u16 BE][0 png│1 jpeg][data]`: a rectangle;
- `[2][w,h,hx,hy u16 BE][png]`: cursor image;
- `[3]`: end of frame;
- `[4]`: PNG refresh.

Text (JSON): `hello`, `size`, `cur{x,y,m}`, `clip{text}`, `pong`, `bye` (with `lock:1` if the
session was locked).

**Browser → server.** Text (JSON):

- `m{x,y}`, `b{b,d}`, `w{d}` (wheel steps);
- the browser's trackpad (see `TRACKPAD.md`):
  - `s{p,x,y}`: fine scrolling in 1/256 of a pixel, with phase `p` (1 begins, 2 continues, 3 ends
    after 150 ms without events). It's told apart from a wheel because it carries `deltaX`,
    fractions or small steps;
  - `pz{p,s,r}`: pinch, with the scale ×65536 relative to the start and the rotation in degrees
    ×256. It comes from the wheel with `ctrlKey`, which is how Chrome and Firefox deliver the
    trackpad pinch (and also a mouse's Ctrl+wheel), or from `gesturechange` in WebKit browsers;
  - the bridge passes them to the session as `RP_SCROLL` and `RP_PINCH`, with clamped values;
- `k{c,d}`, with evdev codes derived from `KeyboardEvent.code`, so the session applies the
  layout;
- `clip{text}`;
- `resize{w,h}`: on connecting and when the window is resized; if the session is on the monitor,
  the monitor wins;
- `ack`, `ping{ts}`.

**Validation** (`rs_ws_consume`, `rs_json_parse`, `rs_browser_input` in `rsec.c`): frames must be
masked, without RSV bits, with a known opcode and the shortest length encoding; control frames at
most 125 bytes and never fragmented; a message at most 2 MB (fragments are joined up to that);
text must be valid UTF-8; a binary message closes the connection (the page never sends one). Each
text message is one flat JSON object (known keys, integers or strings, no duplicates); positions
are clamped to the screen, key codes are keyboard keys only (1–248), the clipboard is at most 1 MB.
Anything else is ignored or closes with 1002/1007/1009.

**Compression:**

- **Still screen:** PNG (lossless).
- **Moving screen** (consecutive frames) and large areas: straight JPEG.
- **When it becomes still:** whatever was JPEG is refreshed as PNG.

**Flow control:** the browser acknowledges each frame (`ack`) only once it has drawn it, and the
session accumulates changes until then. A slow network gets fewer frames, but always the current
state.

## Process and privileges

1. `lord-sessiond` (root) accepts the connection. Refused addresses (blocked, not allowed, too many
   connections) are closed there, before TLS. Otherwise a child handles it (48 at most, 8 per
   address; 32 signed-in browsers).
2. That child does TLS and reads one request under a 15 s deadline (a 90 s alarm backs it up). With
   a valid cookie, `/ws` finds the user's session or creates it (`sess.c`, with the same lock as
   the monitor).
3. **It forks the bridge, which drops root for good:** it unmaps `lord-sessiond`'s shared memory,
   closes every descriptor but the TLS socket, the session socket and a pipe to its parent, clears
   the environment, then `initgroups`, `setresgid`, `setresuid`, checks that the real, effective
   and saved ids are the user's and that it can't get root back, sets `no_new_privs`, not dumpable,
   and dies with its parent. TLS, compression and the bridge run as the user. The root parent keeps
   nothing of the connection: it only waits, and when the bridge reports that the session was
   locked, it ends every cookie of that user.

OpenSSL (TLS) is only in `lord-sessiond`; `lord-server` doesn't link it. The certificate is
self-signed P-256, with CN equal to the machine name and ten years of validity. It's in
`/etc/lordos/remote/cert.pem` (`key.pem` 0600) and is created the first time remote is turned on.

## Threats

| Threat                                 | What's there                                                                             |
|----------------------------------------|------------------------------------------------------------------------------------------|
| someone listens on the network         | TLS 1.2+ with AEAD ciphers only; the certificate fingerprint is shown in Settings          |
| password guessing                      | 3 failures block the address (15 min, 1 h, 24 h), per-account slowdown, journal            |
| guessing from many addresses           | each address gets 2 tries before its block; the per-account slowdown caps the rate per user |
| user enumeration                       | no user list; same message and the same 1.25 s for unknown users and wrong passwords      |
| cookie theft or fixation               | `__Host-`, `HttpOnly`, `Secure`, `SameSite=Strict`, 12 h, stored hashed, new at each sign-in, ended by lock, logout and password change |
| another page uses the session (CSRF)   | `Origin` = `https://` + `Host` required on `/login`, `/logout` and `/ws`; `frame-ancestors 'none'` |
| floods and slow clients                | 20 new connections per address then 2/s, 8 open per address, 48 in all, 15 s to send the request |
| bug in the bridge                      | it runs as the user who owns the session, without root's shared state or descriptors      |
| access from the internet               | refused by default (local network only); use a VPN (Settings → Network → VPN)             |

## Security analysis (0.8.3)

A review of everything the network can reach, done for 0.8.3: the HTTP parser, the login, the
cookie, CSRF, the WebSocket and the input messages, the privilege drop, resource limits, TLS,
headers, logging and the lock screen. Every finding below was fixed in the same release; the tests
are in `server/tests/remote_sec_test.c` (207 checks), `server/tests/remote_fuzz.c` (ASan+UBSan,
100,000 random and mutated inputs on every `make -C server test`), and the attack run on VM 122
listed after the table.

| # | Severity | What | Fix | Test |
|---|----------|------|-----|------|
| 1 | High | The 32-connection cap only counted signed-in browsers: any host could open unlimited connections, each a root child that waited up to 15 s per read (forever if bytes trickled in). | A child per connection only if the address is allowed, not blocked, within 20 new connections then 2/s, under 8 open from it and 48 in all; 15 s for handshake + request, 90 s alarm. | 14 parallel connections: 8 served; 60 quick requests: 8 served, 52 refused; one header byte per second: cut at 16.1 s; TCP without a TLS handshake: cut at 15.0 s. |
| 2 | High | After dropping root the bridge still had `lord-sessiond`'s shared memory mapped read-write: it could forge a cookie for any other user, erase blocks, hold the lock (freezing the root process) or plant a pid that root later signalled when remote was turned off (`kill(-1)` = every process). | The bridge is forked after the handshake and unmaps it; root only signals pids from its own private list. | `/proc` of a live bridge: uid 1003 (real, effective, saved), no shared mapping, fds 0–2 + socket + session + pipe, `NoNewPrivs 1`. |
| 3 | Medium | User enumeration by timing: an unknown or refused account returned before `crypt`, a wrong password after it. | Dummy hash with the same algorithm and cost; every failure answered 1.25 s after the check began. | 8–24 tries each: unknown 1254.2 ms, existing user with a wrong password 1254.1 ms, `root` 1254.6 ms, a system account 1254.8 ms (medians). |
| 4 | Medium | No lockout (only growing waits, in a 64-entry table an attacker could cycle); no restriction on where connections came from. | Lockout per address (/64 for IPv6), 15 min → 1 h → 24 h, persistent; local network only by default; manual lists. | 3 wrong → blocked page, 4th refused before TLS; still blocked after restarting `lord-sessiond`; unblocked from Settings; "only these addresses" refused the Pi and served the listed Mac. |
| 5 | Medium | A request without `Origin` passed the same-origin check; logout was a `GET` (any site could sign you out). | `Origin` required and equal to `https://` + `Host` on `/login`, `/logout`, `/ws`; `Sec-Fetch-Site` must agree; logout is a `POST`. | foreign `Origin`, `http://` origin, none, `cross-site`: 403; `GET /logout`: 405. |
| 6 | Medium | Cookie: matched by substring (`xlordos_remote=` counted), no `__Host-` prefix (another service on the same host could plant one), kept in clear in shared memory, unaffected by a password change. | `__Host-lordos_remote`, exact parse, two of them refused, only SHA-256 stored, bound to the shadow hash, the old cookie dropped at sign-in. | forged, old name, duplicated: 403; after `chpasswd` and after logout: 303 to the sign-in page / 403 on `/ws`. |
| 7 | Medium | Locking the screen ended only the cookie of the browsers connected at that moment; a browser signed in earlier could come back to the session. | Lock ends every cookie of that user (the root process for a lock on the monitor, the bridge's root parent otherwise). | a second, unconnected cookie: 200 before the lock, refused after. |
| 8 | Low | HTTP parser: last duplicate header won, `Content-Length` read with `atol`, `Transfer-Encoding` ignored, bare LF, folding, any method served pages, bytes after the body accepted. | `rs_parse_head`: strict request line, 40 headers, 8 KB head, duplicates refused, `Transfer-Encoding` → 501, 4 KB body, one request per connection, method per path. | 22 malformed requests answered 400/405/413/414/415/431/501 as expected. |
| 9 | Low | Form parser: first duplicate won, `%00` cut the value, long values truncated silently, unknown fields ignored, no `Content-Type` check, 256-byte password limit. | `rs_parse_login`: exactly `user` and `pass`, strict `%XX`, user name rules, password 1–1024 bytes, no NUL; form type required; no query on `/login`. | 33-character name, 1025-byte password, `%00`: refused (and counted); duplicate/unknown field, `%zz`: 400; `text/plain`: 415. |
| 10 | Low | WebSocket: no RSV/opcode/length checks, pings up to 2 MB echoed back, fragments silently dropped, no UTF-8 check. | `rs_ws_consume` (see Browser protocol). | unmasked frame → close 1002; fuzzed. |
| 11 | Low | Input messages were read with `strstr` (a string could smuggle `"x":` into a number); `BTN_*` codes up to 0x2ff accepted as keys. | Strict flat JSON; keys 1–248; all values clamped. | unit tests. |
| 12 | Low | TLS used OpenSSL's default list (CBC suites in TLS 1.2), compression and renegotiation defaults; key permissions not checked. | ECDHE-ECDSA AES-GCM / ChaCha20-Poly1305 only, TLS 1.3 AEAD suites, X25519/P-256/P-384, no compression or renegotiation, server order; `key.pem` forced to root 0600. | TLS 1.0/1.1: protocol alert; TLS 1.2 accepts exactly the 3 AEAD suites; TLS 1.3 the 3 standard ones (CCM refused); compression none. |
| 13 | Low | CSP allowed inline scripts; no `Permissions-Policy`, COOP or CORP. | Hash-based CSP, `form-action`, `base-uri`, the rest of the headers. `Referrer-Policy` stays `same-origin`: with `no-referrer` browsers send `Origin: null` on the page's own form. | header dump; Chrome sign-in works under the CSP. |
| 14 | Low | Accounts: shells matched by substring only, root-group accounts allowed, `/etc/nologin`, forced password change and expired passwords ignored. | Shell from `/etc/shells`, no gid 0, never `root`, the shadow aging rules. | `root` refused like a wrong password. |
| 15 | Low | Log injection: `Origin`/`Host` logged raw (a bare LF forged a journal line); attempted names logged only when valid. | Everything logged is cleaned to printable ASCII; attempted names logged sanitized. | journal check. |
| 16 | Info | The privilege drop didn't check saved ids or groups, kept the environment and every inherited descriptor, and the bridge outlived `lord-sessiond` (the parent-death signal is cleared by `setuid`). | See "Process and privileges". | `/proc` check above. |
| 17 | Info | After a failure the user name went into the URL (`/?e=1&u=…`): a password typed into the user field ended up in the history. | The page keeps the name in `sessionStorage` and clears the address bar. | browser check. |

**Greeter and lock.** A remote browser can't unlock the monitor: the monitor shows the login
screen (a separate `lord-server` as `lordos-greeter`), and the session only reaches `lord-sessiond`
through the seat connection, whose requests are lock, switch user and power. Signing in from the
browser resumes the user's session in the background, never on the monitor. Locking the monitor
ends the remote viewers and every remote cookie of that user; a browser must sign in again with the
password. Locking from the browser does the same.

**What stays as it is.** `lord-sessiond` reads `/etc/shadow` itself instead of PAM (no PAM stack
to configure; the account rules PAM would apply are listed under Login). The certificate is
self-signed: compare the fingerprint the first time. An attacker on the LAN with many addresses
gets 2 tries per address; the per-account slowdown limits that. Two-step sign-in is a proposal in
`ROADMAP.md`.

**Attack run (VM 122, Debian 13, October 19)** from the development Mac (172.17.0.5) and the
Raspberry Pi (172.17.0.24) with a stdlib Python client, OpenSSL's `s_client` on the VM, and a
headless browser for the pages: normal sign-in, desktop over WebSocket and sign-out; brute force
and persistence; timing; 22 malformed requests; CSRF; WebSocket without, with forged, logged-out,
duplicated or out-of-date cookies, wrong key, version or origin; slowloris; connection flood; TLS
scan; headers; lock. All results are in the table.

![Sign-in page: blocked](remote-desktop-login-blocked.png)

![Sign-in page: invalid user name](remote-desktop-login-invalid.png)

![Settings → Sharing: blocked addresses (dark)](settings-sharing-blocked-dark.png)

![Settings → Sharing: only these addresses](settings-sharing-allowlist-light.png)

## Measurements (VM 118: Xeon Silver 4108 at 1.8 GHz, 4 cores, no GPU, 1280×800)

| Situation                                         | lord-server (session) | bridge (lord-sessiond) | monitor | network      |
|---------------------------------------------------|----------------------|------------------------|---------|--------------|
| idle desktop, watched from the web                | 1–2 %                | 0 %                    | —       | < 1 KB/s     |
| just moving the mouse (web)                       | 2–3 %                | 0–1 %                  | —       | 25–40 KB/s   |
| dragging a large window (web)                     | 95 %                 | 13 %                   | —       | 380 KB/s, 4–5 frames/s |
| dragging a large window on the monitor (CPU)      | 80 %                 | —                      | —       | —            |
| session nobody is watching                        | ~1 %                 | —                      | —       | —            |

- **Memory per session:** about 50 MB. The login screen uses 41 MB. With software GPU
  (llvmpipe), the monitor session uses about 145 MB.
- **With JPEG while moving:** the bridge went from 53 % to 13 % CPU while dragging.
- **Frames per second:** in this VM the limit is the desktop's CPU composition (blur and
  transparency), not the network or the compression.

## Stage 2: audio (designed, pending)

The idea is for the bridge, which already runs as the user, to be one more client of that
session's PipeWire:

- **From the machine to the browser:**
  - it takes the monitor of the default output;
  - compresses it with Opus (20 ms, 48 kHz stereo, about 64 kbit/s);
  - sends it in a new binary message, `[5][timestamp][opus]`;
  - the browser decodes it with WebCodecs `AudioDecoder` and plays it with an `AudioWorklet`,
    with a buffer of about 60 ms. Without WebCodecs, it falls back to Opus in WASM.
- **Microphone (optional, by permission):** the reverse path, with an `Audio/Source` node in the
  session.
- **Multiple viewers:** if there are several, the audio goes to just one, the last one that asked
  for it.
- **Dependency:** libopus, only in lord-sessiond.

## Remote login (SSH)

Settings → Sharing also has **«Remote login (SSH)»**: other computers open a terminal on this one
with any SSH client (`ssh <user>@<address>`), with a user and password of the machine. It's the
standard OpenSSH server (`openssh-server`), not something of our own.

![Settings → Sharing: remote login on](settings-sharing-ssh-light.png)

- **It's off by default on every install:** the live ISO, a disk installed from it, the Raspberry
  Pi image and the Debian installer. The ISO and the installed system don't even ship
  `openssh-server`; the Pi image ships it disabled. The switch shows the real state: whether
  `ssh.service` (or `ssh.socket`) is enabled to start at boot, and whether `sshd` is running.
- **Turning it on:**
  1. If `openssh-server` isn't installed, Settings installs it with the package engine's own
     action, `pkg-install openssh-server openssh-sftp-server` (the Store's path, `server/src/pkg`
     and `dpkg`; `openssh-sftp-server` is a Recommends the engine skips, and `scp`/`sftp` need
     it). With no package lists yet (a fresh install) it runs `pkg-update` once first. It needs
     the internet.
  2. Then `ssh-set on`: it creates the host keys if missing (`ssh-keygen -A`), checks the
     configuration (`sshd -t`) and runs `systemctl enable --now ssh.service`. It stays on across
     reboots.
- **Turning it off:** `ssh-set off` runs `systemctl disable --now ssh.service ssh.socket`. The
  package stays installed. Sessions already open aren't cut (Debian's unit uses
  `KillMode=process`); new ones are refused and port 22 closes, also after a reboot.
- **How to connect:** when it's on, the page shows `ssh <your user>@<address>` for each LAN IPv4
  address and for the machine name, and the **ED25519 host key fingerprint** (`SHA256:…`, computed
  by `lord-server` from `/etc/ssh/ssh_host_ed25519_key.pub`, the same as
  `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`). Compare it with the one `ssh` shows the
  first time it connects.
- **Who can log in:** all users with a password (the default), or **only administrators**
  (`ssh-allow admins` writes `/etc/ssh/sshd_config.d/11-lordos-allow.conf` with
  `AllowGroups sudo admin wheel`, checks it with `sshd -t` and reloads the server; `ssh-allow all`
  removes it). **Root never logs in.**
- **Raspberry Pi:** the image's `ssh` switch file on the boot partition keeps working
  ([RASPBERRY.md](RASPBERRY.md)): it enables `ssh.service`, and the Settings switch shows it on.
- **Debian installer** (`install.sh`): it never touches an existing SSH server (on a server or VPS
  it may be how you are connected); it only reports its state at the end.

![Settings → Sharing: remote login off (dark)](settings-sharing-ssh-off-dark.png)

### Safe defaults

The `lordos` package ships `/etc/ssh/sshd_config.d/10-lordos.conf`, read only once
`openssh-server` is installed. Debian's `sshd_config` includes that folder at the top and `sshd`
keeps the first value it reads, so it wins over the rest:

- `PermitRootLogin no`.
- Password logins stay on, because people expect them. **Key logins are safer**: copy your key
  with `ssh-copy-id <user>@<address>` and, once it works, you may add a file in that folder with
  `PasswordAuthentication no`.
- Nothing else is changed: Debian's defaults (key types, ciphers, `MaxAuthTries`) stay.

### Privileges

The privileged part is two closed actions of the admin helper (`server/src/remote/ssh_admin.c`,
`SSH_ADMIN_ACTIONS` in `admin.c`), only for administrators (sudo, admin or wheel), like the
`remote-*` actions:

| Action                  | Argument          | What it runs (fixed paths)                                              |
|-------------------------|-------------------|--------------------------------------------------------------------------|
| `ssh-set`               | `on` or `off`     | `ssh-keygen -A` if there's no host key, `sshd -t`, `systemctl enable --now` / `disable --now` |
| `ssh-allow`             | `all` or `admins` | writes or removes the fixed `AllowGroups` drop-in, `sshd -t`, `systemctl try-reload-or-restart` |

Nothing the user types reaches a command line or sshd's configuration, and no package is installed
by these actions: that stays in the package engine. `lord-server` only reads the state
(`lord.ssh.status()` in `server/src/ui/js_sys.c`: the unit files, `/proc/*/comm` for a root
`sshd`, the drop-in and the public host key), and doesn't run anything.

### Firewall

Nothing to open. `lord-netperm`'s per-app rules apply to programs started from a LordOS session
(they live in its cgroup tree); `sshd` is a system service in `system.slice/ssh.service`, so the
rules never block it, and LordOS adds no system-wide packet filter. If you run your own (nftables,
a router), allow TCP port 22 from the local network only. As with remote desktop, to use it from
the internet, use a VPN instead of opening the port.

### Tested (VM 122, Debian 13, 0.8.1 development build)

- With `openssh-server` purged, the switch was off and port 22 closed. Turning it on installed
  `openssh-server` and `openssh-sftp-server` through `pkg-install` (about 20 s), created new host
  keys and started the server; the fingerprint on the page matched
  `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub`.
- From another machine: key login, password login and `scp` worked; `root` was refused
  (`sshd -T`: `permitrootlogin no`, `passwordauthentication yes`).
- «Only administrators»: `bob` (not in sudo) was refused («none of user's groups are listed in
  AllowGroups»), `lord` logged in. Back to «All users» removed the drop-in.
- Reboot with it on: still enabled and listening. Turned off: `ssh.service` disabled and
  inactive, port 22 closed, and still closed after another reboot.
- A standard user (lord taken out of `sudo` for the test): the page shows the administrator note
  and the switch does nothing; the server stayed off.

## Files

| Path                                  | What it is                                                 |
|---------------------------------------|------------------------------------------------------------|
| `server/src/remote/conn.c`            | one connection: HTTP, login, WebSocket, bridge             |
| `server/src/remote/rsec.c`            | strict parsers: request head, form, cookie, WebSocket, JSON |
| `server/src/remote/ipnet.c`, `lockout.c` | addresses and networks, lockout and connection rate     |
| `server/src/remote/enc.c`             | PNG/JPEG (stb_image_write, vendored in third_party/stb)    |
| `server/src/remote/login.html`, `client.html` | pages (embedded in the binary)                      |
| `server/src/remote/remote_admin.c`    | `remote-*` actions                                         |
| `server/src/remote/ssh_admin.c`       | `ssh-*` actions (remote login)                             |
| `tools/iso/rootfs/etc/ssh/sshd_config.d/10-lordos.conf` | sshd defaults shipped by the `lordos` package |
| `apps/settings/Sharing.jsx`, `sdk/remote.js` | Settings → Sharing and the notice on the monitor    |
| `/etc/lordos/remote/`                 | `config`, `access`, `cert.pem`, `key.pem`, `fingerprint`   |
| `/var/lib/lordos/remote/`             | `blocklist`, `requests` (root only)                        |

## Screenshots

- ![same session from the browser](remote-desktop-browser-same-session.png)
- ![notice on the monitor](remote-desktop-monitor-notice.png)
- ![bob with his own desktop](remote-desktop-own-session.png)
- ![Latin American keyboard from the browser (ñ)](remote-desktop-keyboard-layout.png)
- ![lock: the browser goes back to the login](remote-desktop-web-locked.png)
- ![remote login on (dark)](settings-sharing-ssh-dark.png)
- ![remote login off (light)](settings-sharing-ssh-off-light.png)
