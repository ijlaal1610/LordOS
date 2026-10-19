#!/bin/sh
# apt-repo.sh — the signed LordOS APT repository on lordos.org (docs/UPDATES.md, docs/RELEASING.md).
#
#   tools/release/apt-repo.sh build   <target> [--notes VERSION=SECTION]… <file.deb>…
#   tools/release/apt-repo.sh publish <target>
#   tools/release/apt-repo.sh verify  <target>
#
# <target> is "staging" (https://lordos.org/apt-staging, for tests) or "live" (https://lordos.org/apt,
# what every LordOS system reads). Both have one suite, "stable", component "main", for amd64 and arm64.
#
#   build    copies the published pool down from S3, adds the .debs, writes the indexes with Debian's
#            apt-ftparchive (in a debian:trixie container when this machine has none), the release notes
#            of each lordos version from CHANGELOG.md (main/notes/ReleaseNotes), and signs Release
#            with the LordOS archive key: InRelease and Release.gpg. Then it checks the signatures.
#   publish  uploads the pool first, then the indexes, the signed files last; the archive keyring to
#            lordos.org/keys/ and lordos.sources next to the repository; invalidates the CDN.
#   verify   downloads InRelease over https AND http (the Store's package engine has no TLS of its
#            own and reads https sources over http), checks the signature and every index hash.
#
# --notes VERSION=SECTION takes the notes of lordos VERSION from CHANGELOG.md section SECTION (for
# example 0.8.1~staging1=Unreleased). By default the section is "VERSION" or "VERSION-<suffix>".
#
# The private key lives ONLY on the maintainer's machine, next to the release key:
#   ~/.config/lordos-release/apt-gnupg   (a GnuPG home, mode 0700; LORDOS_APT_GNUPGHOME overrides)
# Never copy it to a VM, a server or the repository. The public key is
# packaging/keys/lordos-archive-keyring.gpg (shipped in the lordos package) and the script refuses to
# sign with any other key. Uploads use only the AWS profile "lordos" (tools/web/deploy.sh's).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PROFILE=lordos
BUCKET=lordos.org
DISTRIBUTION=E35DXDFJ83X7Z8
SUITE=stable
ARCHES="amd64 arm64"
KEYRING=$ROOT/packaging/keys/lordos-archive-keyring.gpg
GNUPGHOME=${LORDOS_APT_GNUPGHOME:-$HOME/.config/lordos-release/apt-gnupg}
WORKDIR=${LORDOS_APT_DIR:-$HOME/.local/share/lordos-apt}
IMAGE=lordos-apt-tools:trixie
export GNUPGHOME

die() { echo "apt-repo.sh: $*" >&2; exit 1; }
usage() { sed -n '3,6p' "$0" >&2; exit 2; }
aws() { command aws --profile "$PROFILE" "$@"; }

[ $# -ge 2 ] || usage
cmd=$1; target=$2; shift 2
case "$target" in
    staging) PREFIX=apt-staging ;;
    live) PREFIX=apt ;;
    *) die "target must be staging or live, not $target" ;;
esac
TREE=$WORKDIR/$target
URL=https://$BUCKET/$PREFIX

# apt-ftparchive and dpkg-deb: this machine's (a Debian build host) or a debian:trixie container's
debtool() {
    if command -v apt-ftparchive >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
        (cd "$TREE" && "$@")
        return
    fi
    command -v docker >/dev/null 2>&1 || die "needs apt-ftparchive and dpkg-deb (apt-utils, dpkg) or docker"
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        printf 'FROM debian:trixie\nRUN apt-get update -q && apt-get install -y -q --no-install-recommends apt-utils dpkg-dev xz-utils && rm -rf /var/lib/apt/lists/*\n' |
            docker build -q -t "$IMAGE" - >/dev/null || die "could not build the $IMAGE container"
    fi
    docker run --rm -u "$(id -u):$(id -g)" -v "$TREE:/repo" -w /repo "$IMAGE" "$@"
}

# the fingerprint of the one key in packaging/keys/lordos-archive-keyring.gpg
expected_fpr() {
    t=$(mktemp -d)
    GNUPGHOME=$t gpg --batch --quiet --import "$KEYRING" 2>/dev/null
    GNUPGHOME=$t gpg --batch --with-colons --list-keys | awk -F: '/^fpr/ { print $10; exit }'
    rm -rf "$t"
}

check_key() {
    [ -d "$GNUPGHOME" ] || die "no signing key in $GNUPGHOME (see docs/UPDATES.md, \"The keys\")"
    FPR=$(expected_fpr)
    [ -n "$FPR" ] || die "cannot read $KEYRING"
    gpg --batch --with-colons --list-secret-keys "$FPR" >/dev/null 2>&1 \
        || die "the secret key for $FPR (packaging/keys/lordos-archive-keyring.gpg) is not in $GNUPGHOME"
}

# CHANGELOG.md section → deb822 "Notes" lines (one leading space, " ." for an empty line)
notes_for() {
    version=$1; section=$2
    if [ -z "$section" ]; then
        section=$(sed -n 's/^## \[\([^]]*\)\].*/\1/p' "$ROOT/CHANGELOG.md" |
            awk -v v="$version" '$0 == v || index($0, v "-") == 1 { print; exit }')
    fi
    [ -n "$section" ] || return 0
    # the release body without its fixed footer (downloads, verification and links)
    "$ROOT/tools/release/release-notes.sh" "$section" 2>/dev/null |
        awk '/^---$/ { cut = NR } { line[NR] = $0 } END { n = cut ? cut - 1 : NR; while (n > 0 && line[n] == "") n--; for (i = 1; i <= n; i++) print line[i] }' |
        sed -e 's/^$/./' -e 's/^/ /'
}

do_build() {
    NOTES_MAP=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --notes) [ $# -ge 2 ] || die "--notes needs VERSION=SECTION"; NOTES_MAP="$NOTES_MAP $2"; shift ;;
            --notes=*) NOTES_MAP="$NOTES_MAP ${1#*=}" ;;
            -*) die "unknown option $1" ;;
            *) break ;;
        esac
        shift
    done
    check_key
    mkdir -p "$TREE/pool/main"
    # the pool is append-only: start from what is published (old versions stay installable)
    if [ -z "${LORDOS_APT_OFFLINE:-}" ]; then
        aws s3 sync "s3://$BUCKET/$PREFIX/pool" "$TREE/pool" --only-show-errors || die "could not read s3://$BUCKET/$PREFIX/pool"
    fi
    for deb in "$@"; do
        [ -f "$deb" ] || die "no such file: $deb"
        mkdir -p "$TREE/incoming"
        cp "$deb" "$TREE/incoming/"
        b=$(basename "$deb")
        name=$(debtool dpkg-deb -f "incoming/$b" Package)
        ver=$(debtool dpkg-deb -f "incoming/$b" Version)
        arch=$(debtool dpkg-deb -f "incoming/$b" Architecture)
        case "$name" in lib?*) dir=$(printf %s "$name" | cut -c1-4) ;; *) dir=$(printf %s "$name" | cut -c1) ;; esac
        dest="pool/main/$dir/$name/${name}_${ver#*:}_$arch.deb"
        if [ -f "$TREE/$dest" ] && ! cmp -s "$TREE/incoming/$b" "$TREE/$dest"; then
            die "$dest is already published with different contents: a version is never replaced (bump it)"
        fi
        mkdir -p "$(dirname "$TREE/$dest")"
        mv "$TREE/incoming/$b" "$TREE/$dest"
        echo "  $dest"
    done
    rm -rf "$TREE/incoming"

    D=dists/$SUITE
    rm -rf "${TREE:?}/dists"
    for a in $ARCHES; do
        mkdir -p "$TREE/$D/main/binary-$a"
        debtool apt-ftparchive --arch "$a" packages pool/main > "$TREE/$D/main/binary-$a/Packages"
        xz -9 -k -f "$TREE/$D/main/binary-$a/Packages"
        gzip -9 -n -k -f "$TREE/$D/main/binary-$a/Packages"
        printf 'Archive: %s\nOrigin: LordOS\nLabel: LordOS\nComponent: main\nArchitecture: %s\n' "$SUITE" "$a" > "$TREE/$D/main/binary-$a/Release"
    done

    # release notes of every lordos version in the pool (Store → Updates shows the candidate's)
    mkdir -p "$TREE/$D/main/notes"
    : > "$TREE/$D/main/notes/ReleaseNotes"
    versions=$(awk '/^Package: lordos$/ { p = 1; next } /^Package:/ { p = 0 } p && /^Version:/ { print $2 }' "$TREE"/$D/main/binary-*/Packages | sort -u)
    for v in $versions; do
        section=""
        for m in $NOTES_MAP; do [ "${m%%=*}" = "$v" ] && section=${m#*=}; done
        body=$(notes_for "$v" "$section")
        [ -n "$body" ] || { echo "  (no CHANGELOG section for lordos $v: no release notes)"; continue; }
        printf 'Package: lordos\nVersion: %s\nNotes:\n%s\n\n' "$v" "$body" >> "$TREE/$D/main/notes/ReleaseNotes"
        echo "  notes for lordos $v"
    done
    xz -9 -k -f "$TREE/$D/main/notes/ReleaseNotes"

    debtool apt-ftparchive \
        -o APT::FTPArchive::Release::Origin=LordOS \
        -o APT::FTPArchive::Release::Label=LordOS \
        -o APT::FTPArchive::Release::Suite="$SUITE" \
        -o APT::FTPArchive::Release::Codename="$SUITE" \
        -o APT::FTPArchive::Release::Architectures="$ARCHES" \
        -o APT::FTPArchive::Release::Components=main \
        -o APT::FTPArchive::Release::Description="LordOS updates for Debian 13 (trixie)" \
        -o APT::FTPArchive::Release::Patterns::=ReleaseNotes -o APT::FTPArchive::Release::Patterns::=ReleaseNotes.xz \
        release "$D" > "$TREE/Release.new"
    mv "$TREE/Release.new" "$TREE/$D/Release"

    # sign on this machine (the key never leaves it): clear-signed InRelease and detached Release.gpg
    gpg --batch --yes --local-user "$FPR" --digest-algo SHA512 --clearsign -o "$TREE/$D/InRelease" "$TREE/$D/Release"
    gpg --batch --yes --local-user "$FPR" --digest-algo SHA512 --armor --detach-sign -o "$TREE/$D/Release.gpg" "$TREE/$D/Release"
    check_signatures "$TREE/$D/InRelease" "$TREE/$D/Release" "$TREE/$D/Release.gpg"
    echo "built $TREE ($(find "$TREE/pool" -name '*.deb' | wc -l | tr -d ' ') packages); publish with: $0 publish $target"
}

# the signatures against the public keyring that the lordos package ships (not the signing home)
check_signatures() {
    t=$(mktemp -d)
    GNUPGHOME=$t gpg --batch --quiet --import "$KEYRING" 2>/dev/null
    GNUPGHOME=$t gpg --batch --quiet --verify "$1" 2>/dev/null || { rm -rf "$t"; die "InRelease does not verify with $KEYRING"; }
    GNUPGHOME=$t gpg --batch --quiet --verify "$3" "$2" 2>/dev/null || { rm -rf "$t"; die "Release.gpg does not verify with $KEYRING"; }
    rm -rf "$t"
    echo "  signatures verified with packaging/keys/lordos-archive-keyring.gpg"
}

do_publish() {
    [ -f "$TREE/dists/$SUITE/InRelease" ] || die "nothing built in $TREE: run $0 build $target first"
    check_signatures "$TREE/dists/$SUITE/InRelease" "$TREE/dists/$SUITE/Release" "$TREE/dists/$SUITE/Release.gpg"
    # 1. packages (never change once published), 2. indexes, 3. the signed files that point at them
    aws s3 sync "$TREE/pool" "s3://$BUCKET/$PREFIX/pool" --cache-control "public, max-age=31536000, immutable" \
        --content-type application/vnd.debian.binary-package --only-show-errors
    aws s3 sync "$TREE/dists" "s3://$BUCKET/$PREFIX/dists" --delete --cache-control "no-cache" \
        --exclude "*/InRelease" --exclude "*/Release" --exclude "*/Release.gpg" --only-show-errors
    for f in Release Release.gpg InRelease; do
        aws s3 cp "$TREE/dists/$SUITE/$f" "s3://$BUCKET/$PREFIX/dists/$SUITE/$f" --cache-control "no-cache" \
            --content-type "text/plain; charset=utf-8" --only-show-errors
    done
    # the sources file of this target (live: byte for byte the one the package installs)
    sed "s#^URIs: https://lordos.org/apt\$#URIs: $URL#" "$ROOT/packaging/apt/lordos.sources" > "$TREE/lordos.sources"
    aws s3 cp "$TREE/lordos.sources" "s3://$BUCKET/$PREFIX/lordos.sources" --cache-control "no-cache" \
        --content-type "text/plain; charset=utf-8" --only-show-errors
    aws s3 cp "$KEYRING" "s3://$BUCKET/keys/lordos-archive-keyring.gpg" --cache-control "no-cache" \
        --content-type application/pgp-keys --only-show-errors
    aws s3 cp "${KEYRING%.gpg}.asc" "s3://$BUCKET/keys/lordos-archive-keyring.asc" --cache-control "no-cache" \
        --content-type "text/plain; charset=utf-8" --only-show-errors
    id=$(aws cloudfront create-invalidation --distribution-id "$DISTRIBUTION" \
        --paths "/$PREFIX/dists/*" "/$PREFIX/lordos.sources" "/keys/*" --query Invalidation.Id --output text)
    echo "published $URL (invalidation $id); check it with: $0 verify $target"
}

do_verify() {
    t=$(mktemp -d)
    trap 'rm -rf "$t"' EXIT
    GNUPGHOME=$t gpg --batch --quiet --import "$KEYRING" 2>/dev/null
    for scheme in https http; do
        u=$scheme://$BUCKET/$PREFIX/dists/$SUITE
        code=$(curl -s -o "$t/InRelease.$scheme" -w '%{http_code}' "$u/InRelease")
        [ "$code" = 200 ] || die "$u/InRelease: HTTP $code (http must be served as is, not redirected: docs/UPDATES.md)"
        GNUPGHOME=$t gpg --batch --quiet --verify "$t/InRelease.$scheme" 2>/dev/null || die "$u/InRelease: bad signature"
        echo "  $u/InRelease: signature good"
    done
    cmp -s "$t/InRelease.https" "$t/InRelease.http" || die "http and https serve different InRelease files (CDN cache? try again in a minute)"
    GNUPGHOME=$t gpg --batch --quiet --decrypt "$t/InRelease.https" 2>/dev/null > "$t/Release"
    awk '/^SHA256:/ { s = 1; next } /^[^ ]/ { s = 0 } s { print $1, $3 }' "$t/Release" | while read -r sum path; do
        case "$path" in */Release) continue ;; esac
        curl -fsS -o "$t/f" "http://$BUCKET/$PREFIX/dists/$SUITE/$path" || die "missing $path"
        got=$(shasum -a 256 "$t/f" | cut -d' ' -f1)
        [ "$got" = "$sum" ] || die "$path: SHA-256 does not match InRelease"
    done
    echo "  every index matches InRelease"
    curl -fsS "http://$BUCKET/$PREFIX/dists/$SUITE/main/binary-amd64/Packages" | awk '/^Package:|^Version:|^Architecture:/' | paste - - - | sed 's/^/  /'
    curl -fsS "http://$BUCKET/$PREFIX/dists/$SUITE/main/binary-arm64/Packages" | awk '/^Package:|^Version:|^Architecture:/' | paste - - - | sed 's/^/  /'
    echo "verified $URL"
}

case "$cmd" in
    build) do_build "$@" ;;
    publish) do_publish ;;
    verify) do_verify ;;
    *) usage ;;
esac
