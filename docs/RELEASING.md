# Releasing LordOS

How a LordOS version goes from `main` to a [GitHub release](https://github.com/lordbasex/LordOS/releases).
The releases page is the project's public changelog: every release is documented in full, from
[CHANGELOG.md](../CHANGELOG.md).

## Versions

- [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0, every release is a beta.
- The CHANGELOG version and the git tag carry the pre-release suffix: `0.8.0-beta`, tag
  `v0.8.0-beta`.
- `package.json` carries the version without the suffix (`"version": "0.8.0"`) and the channel
  separately (`"lordos": {"channel": "beta"}`; empty for a stable release). The file names use
  that version: `lordos_0.8.0_amd64.deb`, `lordos-live-0.8.0-amd64.iso`,
  `lordos-0.8.0-rpi5-arm64.img.xz`.
- The release title is **`LordOS <version>`**, for example `LordOS 0.8.0-beta`.
- Every beta (any version with a suffix) is published as a **pre-release**. Only a version without
  a suffix is a normal release.

## The changelog

[CHANGELOG.md](../CHANGELOG.md) follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/):

- Changes go under `## [Unreleased]` as they land, in `### Added`, `### Changed`, `### Deprecated`,
  `### Removed`, `### Fixed` and `### Security` (LordOS also uses `### Known issues and
  limitations`, `### Upgrading` and `### Verifying`).
- Bullets are written for users first (what you can do now, what was wrong), then technical detail
  where useful, with links to the docs (`docs/…`).
- Only what the code and the docs show. Measured numbers come from the test docs
  ([ISO.md](ISO.md), [RASPBERRY.md](RASPBERRY.md)…).
- The wording rules apply: a free GNU/Linux distribution based on Debian with its own desktop
  environment; generic names, no commercial brand names; English only.

## Steps

1. **Changelog.** Rename `## [Unreleased]` to `## [<version>] - <YYYY-MM-DD>` (the release date),
   add a new empty `## [Unreleased]` above it, and update the link references at the bottom:

   ```
   [Unreleased]: https://github.com/lordbasex/LordOS/compare/v<version>...HEAD
   [<version>]: https://github.com/lordbasex/LordOS/compare/v<previous>...v<version>
   ```

   Also bump `package.json` (`version`, `lordos.channel`) and commit both.
2. **Tag.** `git tag -a v<version> -m "LordOS <version>"` on that commit, and push the tag.
3. **Build** from the tag: the ISO on the build VM (`tools/iso/build.sh`, [ISO.md](ISO.md)), the
   Raspberry Pi 5 image (`tools/rpi/build-img.sh`, [RASPBERRY.md](RASPBERRY.md)) and the `.deb`
   for amd64 and arm64. Copy them, and `tools/install/install.sh` as `install.sh`, into one release
   folder.
4. **SBOM.** On each build VM, `tools/release/make-sbom.sh lordos_<v>_<arch>.deb`, and put the
   `.spdx.json` files in the release folder ([COMPLIANCE.md](COMPLIANCE.md)).
5. **Sign.** On the maintainer's machine, `tools/release/sign-release.sh <release-dir>`: it writes
   `SHA256SUMS` and `SHA256SUMS.sig` and checks them against the public key
   ([SIGNING.md](SIGNING.md), "Release files"). Then copy `tools/release/lordos-release.pub.pem`
   into the folder too (it is not covered by `SHA256SUMS`: it is checked by its fingerprint).
6. **Release notes.** `tools/release/release-notes.sh <version> > notes.md` prints the
   changelog section for that version (relative links made absolute for the tag, hard-wrapped
   lines joined, its "Verifying" part replaced by the footer) plus a fixed footer: the downloads
   table, the verification steps and links to lordos.org, the paper, the docs and the security
   page. Read it once before publishing.
7. **Publish:**

   ```sh
   gh release create v<version> --title "LordOS <version>" --prerelease --verify-tag \
       --notes-file notes.md <release-dir>/*
   ```

   Leave out `--prerelease` only for a version without a suffix. The Debian installer
   (`install.sh`) finds betas too: `releases/latest` skips pre-releases, so it falls back to the
   newest release of any kind ([INSTALL-DEBIAN.md](INSTALL-DEBIAN.md)).
8. **Check:** download from the release page and run the verification commands from the notes;
   run `curl -fsSL https://lordos.org/install.sh | sh` on a clean Debian 13; update the website if
   it names the current version.

A release that is pulled after publishing stays in the changelog, marked `[YANKED]`, with a line
saying why.
