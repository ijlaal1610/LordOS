# License compliance and the SBOM

How LordOS keeps track of what it ships and under which licenses, and the software bill of
materials (SBOM) published with each release. The website summary is at
[lordos.org/legal/standards](https://lordos.org/legal/standards/); the user-facing license page is
[lordos.org/legal/licenses](https://lordos.org/legal/licenses/).

## The license inventory

- `LICENSE`: LordOS's own code, MIT.
- `THIRD_PARTY.md`: every component bundled in the repository (`third_party/`), the fonts and data
  files in `assets/`, the npm packages bundled into the theme JavaScript, and the Debian libraries
  LordOS links dynamically, each with its license. A new bundled component, font or data file is
  added there in the same change that adds it.
- The `.deb` carries a machine-readable `usr/share/doc/lordos/copyright` (Debian copyright format,
  written by `tools/iso/mkdeb.sh`), and each font ships its license text next to it
  (`assets/fonts/*-LICENSE.txt`).
- Debian packages keep their own `/usr/share/doc/<package>/copyright` on the installed system.
- **Raspberry Pi packages mirrored in the LordOS APT repository** (arm64, since 0.8.4.1;
  `tools/release/rpi-mirror.list`, [UPDATES.md](UPDATES.md), "Raspberry Pi firmware packages") are
  copied byte for byte, each with its own `copyright` inside: `rpi-eeprom` (scripts BSD-3-Clause;
  bootloader images under Raspberry Pi's binary licence, which allows redistribution of unmodified
  binaries for use with Raspberry Pi devices with the notice), `raspi-utils` and its parts
  (BSD-3-Clause) and `kms++` (MPL-2.0). For the MPL-2.0 binaries the corresponding source is the
  `kms++` source package of the same version in Raspberry Pi's archive (`deb-src
  http://archive.raspberrypi.com/debian trixie main`) and upstream at
  `https://github.com/tomba/kmsxx`; before Raspberry Pi's archive drops that version, its source
  package has to be kept next to the binaries (open item).

## The SBOM

`tools/release/make-sbom.sh` writes an [SPDX 2.3](https://spdx.github.io/spdx-spec/v2.3/) JSON
document for a built package:

| Part | What | Version from |
|---|---|---|
| `lordos` | the `.deb` itself: version, architecture, SHA-256, download URL on GitHub Releases, MIT | `dpkg-deb -f` |
| Bundled code | QuickJS, Monocypher, stb_image, stb_image_write, stb_truetype, the EFF and SecLists word lists, react, react-reconciler, scheduler, the Unicode data | `third_party/*/VERSION`, the stb headers, `package-lock.json`, `server/src/ui/unidata.h` |
| Fonts | Inter, Plus Jakarta Sans, JetBrains Mono (OFL-1.1), with the SHA-256 of each `.ttf` in the package | the font's revision (`fc-scan`, when fontconfig is installed) |
| Data | DB-IP Lite Country (CC-BY-4.0) and Natural Earth (public domain), with the SHA-256 of the files in the package | not versioned |
| Debian packages | every package in the `.deb`'s `Depends` (`DEPENDS_ON`) and `Recommends` (`OPTIONAL_DEPENDENCY_OF`), with a `pkg:deb/debian/…` purl | `dpkg-query` on the machine that runs the script |

For an alternative (`libssl3t64 | libssl3`) the first one installed is listed. A dependency that is
not installed on the machine is listed without a version and the script prints a warning. Debian
package licenses are `NOASSERTION` in the SBOM; the authoritative text is each package's
`copyright` file.

### Generating it

On the Debian 13 build VM, after building the package and with its dependencies installed (so the
versions are the ones the release was built against):

```sh
tools/release/make-sbom.sh out/lordos_0.8.0_amd64.deb            # -> out/lordos_0.8.0_amd64.spdx.json
LORDOS_TAG=v0.8.0-beta tools/release/make-sbom.sh out/lordos_0.8.0_amd64.deb release/lordos_0.8.0_amd64.spdx.json
```

`LORDOS_TAG` sets the release tag used in the download URL (default `v<version>`). The script
needs `dpkg-deb`, `dpkg-query` and `python3`; `fc-scan` is optional.

### Publishing it

Starting with v0.8.0-beta, the SBOM of each architecture is attached to the GitHub release next to
`SHA256SUMS`. Put it in the release folder **before** `tools/release/sign-release.sh`: that script
hashes every file in the folder, so the SBOM is listed in `SHA256SUMS` and covered by the release
signature (see [SIGNING.md](SIGNING.md), "Release files").

## Standards

Our processes are **aligned with**, not certified against, these standards. What is in place and
what is still missing is listed on [lordos.org/legal/standards](https://lordos.org/legal/standards/):

- ISO/IEC 29147 and ISO/IEC 30111: vulnerability disclosure and handling (`SECURITY.md`, the
  security policy and `/.well-known/security.txt`, RFC 9116).
- ISO/IEC 5230 (OpenChain): this inventory, the license notices and the SBOM.
- ISO/IEC 18974 (OpenChain security assurance): the SBOM plus Debian's security updates for the
  packages we depend on.
- WCAG 2.2 AA as the website's accessibility target.

Gaps today: no formal OpenChain conformance self-certification, no automated matching of the SBOM
against vulnerability databases, no SBOM for the ISO and Raspberry Pi images as a whole (only for
the `.deb` and what it depends on), and no third-party audit.
