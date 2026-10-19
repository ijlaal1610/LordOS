#!/bin/sh
# build-mac.sh — the Mac download (.app and .dmg) on a Mac with Rust and Node.
#   desktop/imager/scripts/build-mac.sh          → desktop/imager/target/release/bundle/
# Unsigned by default. With a signing identity in the keychain, set APPLE_SIGNING_IDENTITY (and the
# notarization variables Tauri documents) before running it; see docs/IMAGER.md, "Signing".
set -eu
cd "$(dirname "$0")/.."
npm ci --no-audit --no-fund
node build-web.mjs
npx tauri build --bundles app,dmg
ls -la target/release/bundle/macos target/release/bundle/dmg
