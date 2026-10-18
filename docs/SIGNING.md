# Signing `.lpk` packages (M8)

Every LordOS app package (`.lpk`) can be **signed**. LordOS verifies the signature in C, at
install time and at load time, to detect modifications and to show where the app comes from. See
also [NATIVE-APPS.md](NATIVE-APPS.md) and [PRIVACY.md](PRIVACY.md).

## What's inside

Besides its files (`manifest.json`, `app.js`, `icon.png`…), a signed `.lpk` carries:

- **`contenido.json`** — the list of all files with their path, size and **SHA-256**:

  ```json
  {
    "format": 1,
    "id": "fractal",
    "version": "1.0.0",
    "signer": { "name": "LordOS (desarrollo)", "key": "3deb6836…" },
    "files": [
      { "path": "manifest.json", "size": 321, "sha256": "…" },
      { "path": "app.js",        "size": 2411, "sha256": "…" }
    ]
  }
  ```

- **`firma.json`** — that manifest signed with **Ed25519**:

  ```json
  { "format": 1, "alg": "ed25519", "key": "3deb6836…", "sig": "…" }
  ```

An **unsigned** `.lpk` can still carry `contenido.json` (hashes only) to detect damage; without
it, it is "Unsigned" and has no hashes.

## Our own cryptography (`server/src/sec` + `server/src/pkg/crypto.c`)

SHA-256 and Ed25519 are **our own code**, small and readable. Ed25519 follows the arithmetic of
**TweetNaCl** (public domain; field 2^255-19 in 16 words of 16 bits); **signing**
(`ed25519_sign`/`ed25519_public`) was added on top of the verification the package engine already
had. The JSON reader is our own and strict (`server/src/sec/json.c`).

**No OpenSSL:** by policy only `lord-sessiond` links it. `.lpk` verification happens in C, at
install time (`lpk_check` in `js_sys.c`) and at load time (modification detection via
`pkg_status`).

### Tests (`make -C server test`)

- `build/sign-test` (**55/55**): SHA-256 against the **NIST** vectors (including the million
  `a`s), Ed25519 against tests 1–3 of **RFC 8032** (public key, byte-for-byte signature,
  verification, rejection of altered signature/message), the JSON reader (valid and invalid), and
  package verification: valid signature, altered file, extra file, mismatched id/version,
  signature from another key and broken signature; plus the signed catalog and revocation.
- `build/pkgstate-test` (**11/11**, VM): the full flow install → "Modified by you" → restore →
  share (signed derivative) → update-by-key rule.

## Trust levels

They are shown in the Store and in the install dialog:

| Level | When | Text |
|---|---|---|
| **LordOS** | signed with the official key (`server/src/sec/keys.h`) | "LordOS" (green) |
| **Verified author** | key listed in the signed catalog | "Verified author: <name>" (blue) |
| **Signed by <name>** | signed with an unknown key | "Signed by <name>" (gray) |
| **Unsigned** | has no `firma.json` | "Unsigned — only install from people you trust" (orange, warning) |

| | |
|---|---|
| ![unsigned](signing-unsigned.png) | ![revoked](signing-revoked.png) |
| An unsigned package warns before installing | A revoked package is not installed, with a clear message |

## Rules

- **Updates**: a new version must be signed with the **same key** as the installed one (like
  Android). An update with a different key is rejected (`pkg_check_update`).
- **Signed catalog** (`catalog.json` + `catalog.sig`): signed by the LordOS key, it carries the
  versions, sizes, hashes, the keys of verified authors and a **revocation list** of keys and
  packages. A revoked package is disabled with a clear message (`catalog_revoked`). The catalog in
  force is the valid one with the highest `serial` between `/usr/share/lordos/catalog.json` and
  `~/.local/share/lordos/catalog/catalog.json`.

## Modify, restore and share (the "business" idea)

- At install time, the **original `.lpk` is kept** in `~/.local/share/lordos/apps/.orig/<id>.lpk`.
- If the user **edits** the installed app, LordOS detects it through the hashes and marks it
  "**Modified by you**", showing which files changed (`pkg_status`).
- "**Restore original**" puts it back exactly as it was (`pkg_restore`).
- "**Share my version**" builds a new `.lpk` signed with the **user's own key** (generated the
  first time in `~/.local/share/lordos/keys/user.json`, permissions 600), with
  `basedOn: <id>@<version> by <author>` in the manifest, so credit is preserved (`pkg_share`).

| | |
|---|---|
| ![modified by you](signing-modified.png) | ![restored](signing-restored.png) |
| "Modified by you": what changed, Restore and Share | "Fractal restored to the original" |

## Tools (`tools/lpk.mjs`)

```sh
node tools/lpk.mjs keygen [--name "Nombre"] [--out archivo]   # Ed25519 key pair
node tools/lpk.mjs <carpeta> [salida.lpk] [--key clave.json | --unsigned]   # package and sign
node tools/lpk.mjs sign <paquete.lpk> --key clave.json        # (re)sign
node tools/lpk.mjs verify <paquete.lpk>                       # show signature and hashes
node tools/lpk.mjs catalog <fuente.json> --key oficial.json [carpeta]   # signed catalog
```

`lpk.mjs` signs while packaging if given `--key`, `$LPK_KEY`, or if
`~/.config/lordos/lpk-key.json` exists. With `--unsigned` the result is "Unsigned" (still with
hashes).

### The LordOS key

Official packages (bundled apps, catalog) are signed with the **LordOS key**. Today there is only
one, the **development** key:

- its **public key** is in the repository: `packaging/keys/lordos-dev.pub`, and compiled into the
  binary in `server/src/sec/keys.h` (on purpose: no file in the home folder can add an official
  key);
- its **private key is NOT in the repository**: it lives on the development machine, in
  `~/.config/lordos-dev/lordos-dev.key` (permissions 600). **Do not commit private keys.**

For a public release, the final key is added to `keys.h` and the development one is removed.

### Release files (not signed yet)

The GitHub release files (the `.deb`, the ISO, the Raspberry Pi image) are listed with their
SHA-256 in `SHA256SUMS`. The Debian installer (`tools/install/install.sh`, see
`INSTALL-DEBIAN.md`) always checks the `.deb` against it. **`SHA256SUMS` itself is not signed
yet**, so that check catches corrupted downloads but not a tampered release page; the installer
says so in its output.

The verification side is already in the installer, waiting for a key:

- `SHA256SUMS.sig` = base64 of the 64-byte Ed25519 signature over the exact bytes of `SHA256SUMS`;
- the public key goes in `RELEASE_PUBKEY_PEM` in `install.sh` (PEM, as OpenSSL 3 reads it). From a
  raw hex key like `packaging/keys/lordos-dev.pub`:
  `printf '302a300506032b6570032100%s' "$(cat key.pub)" | xxd -r -p | openssl pkey -pubin -inform DER`;
- when the key is set, a missing or invalid signature stops the install before the `.deb` is
  downloaded (tested with a throwaway key through `LORDOS_PUBKEY_FILE`, a test-only override).

TODO(release-signing), before the first public release:

1. Generate the release key offline and keep the private half off the build VMs.
2. Sign: `openssl pkeyutl -sign -rawin -inkey release.key.pem -in SHA256SUMS | base64 -w0 > SHA256SUMS.sig`,
   and upload `SHA256SUMS.sig` with the release.
3. Put the public key in `RELEASE_PUBKEY_PEM`, and publish its fingerprint on the website and in
   the README, so people who download the ISO by hand can check `SHA256SUMS.sig` too.

## Files

| Path | What it is |
|---|---|
| `server/src/pkg/crypto.c` | SHA-256/512 and Ed25519 (verify and sign), our own |
| `server/src/sec/sign.c` | package and catalog verification, trust levels, revocation |
| `server/src/sec/pkgstate.c` | saved original, "Modified by you", restore, share, update rule |
| `server/src/sec/json.c` | strict JSON reader |
| `server/src/sec/keys.h` | official public keys |
| `server/src/ui/js_sec.c` | `lord.sign` for the SDK (status, restore, share, userKey) |
| `tools/lpk.mjs` | package, sign, verify, keygen and catalog |
| `packaging/keys/lordos-dev.pub` | development public key |
