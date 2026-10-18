#!/bin/bash
# files.sh — "no access to your files" for Linux apps: the launcher (src/apps.c) starts a
# restricted app under Landlock (sec/sandbox.c, sandbox_apply_files). Uses build/launch-cli, the
# real launcher code outside the desktop. Applies at the next launch.
#
#   bash server/tests/fwlinux/files.sh        (from server/, after make -C server test)
#
# 1-3: a stand-in app (files_probe.py) through a .desktop: it can't read ~/Documents, the home
#      folder or firewall.json, can use its own folders, /tmp and the runtime dir, and can open
#      the one file it was opened with (not the one next to it).
# 4:   a real GTK 4 app (gtk4-print-editor, under strace) opened with a file: it starts, reads
#      that file, and is refused ~/Documents.
. "$(dirname "$0")/lib.sh"
D=$(cd "$(dirname "$0")" && pwd)
CLI=$D/../../build/launch-cli
[ -x "$CLI" ] || { echo "missing build/launch-cli (make -C server test)"; exit 2; }
APPS=~/.local/share/applications
mkdir -p "$APPS" ~/Documents ~/fwlinux-open
echo secret > ~/Documents/fwlinux-secret.txt
echo "opened with" > ~/fwlinux-open/note.txt
echo sibling > ~/fwlinux-open/fwlinux-sibling.txt
mkdir -p ~/.config/fwprobe ~/.cache/fwprobe
PROBE=/tmp/fwlinux-files-probe-$U.py       # outside the home folder: the restricted app must be able to run it
cp "$D/files_probe.py" "$PROBE"
cat > "$APPS/fwprobe.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Files restriction probe
Exec=python3 $PROBE %f
MimeType=text/plain;
NoDisplay=false
DESK
OUT=/tmp/fwlinux-files-$U.json
probe() {      # probe [file]: launches and waits for the result
    rm -f "$OUT"
    "$CLI" fwprobe "$@" >/dev/null 2>&1
    for i in $(seq 1 50); do [ -s "$OUT" ] && break; sleep 0.1; done
}
val() { python3 -c "import json;print(json.load(open('$OUT')).get('$1','missing'))" 2>/dev/null; }
expect() { local key=$1 want=$2 got; got=$(val "$key"); case "$got" in $want*) ok "$key → $got";; *) bad "$key → $got (expected $want)";; esac; }

echo "1. not restricted: the app reads ~/Documents"
restrict desktop:fwprobe ""
probe
expect read_documents ok
expect list_home ok

echo "2. no-files: only its own folders, the system, /tmp and the runtime dir"
restrict desktop:fwprobe no-files
probe
expect read_documents "Permission denied"
expect list_documents "Permission denied"
expect list_home "Permission denied"
expect read_firewall_json "Permission denied"
expect write_autostart "Permission denied"
expect read_ssh "Permission denied"
expect read_system ok
expect write_tmp ok
expect write_own_config ok
expect write_own_cache ok
expect rename_in_own_config ok
expect read_desktop_files ok
expect write_runtime_dir ok

echo "3. no-files, opened with a file (\"Open with\")"
probe ~/fwlinux-open/note.txt
expect read_opened_file ok
expect write_opened_file ok
expect read_next_to_opened "Permission denied"
expect read_documents "Permission denied"

if command -v gtk4-print-editor >/dev/null && command -v strace >/dev/null && [ -S "/run/user/$U/wayland-0" ]; then
    echo "4. a real GTK 4 app (gtk4-print-editor, traced with strace), restricted"
    TR=/tmp/fwlinux-gtk-$U.trace
    gtk() {    # gtk <name> <Exec arguments> [file]: a .desktop for the editor, launched restricted
        cat > "$APPS/$1.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=GTK editor (files test)
Exec=strace -f -qq -e trace=openat -o $TR gtk4-print-editor $2
MimeType=text/plain;
DESK
        restrict "desktop:$1" no-files
        rm -f "$TR"
        WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/$U "$CLI" "$1" ${3:+"$3"}
        sleep 4
    }
    # a: its command line names a file in ~/Documents (not passed by "Open with"): refused
    gtk fwgtkdoc "$HOME/Documents/fwlinux-secret.txt"
    PID=$(pgrep -u "$U" -n -x gtk4-print-edit)
    [ -n "$PID" ] && ok "it starts and stays up under the restriction (pid $PID)" || bad "gtk4-print-editor did not stay up"
    [ -n "$PID" ] && grep -q "^NoNewPrivs:.*1" /proc/$PID/status && ok "no_new_privs is set (Landlock)" || bad "no_new_privs not set"
    grep -E "Documents/fwlinux-secret.txt.*EACCES" "$TR" >/dev/null && ok "its open of ~/Documents/fwlinux-secret.txt → EACCES" || bad "~/Documents was not refused: $(grep fwlinux-secret "$TR" | head -1)"
    [ -n "$PID" ] && kill "$PID" 2>/dev/null; sleep 0.5
    # b: opened with a file ("Open with" → %f): that file is readable
    gtk fwgtkopen "%f" "$HOME/fwlinux-open/note.txt"
    PID=$(pgrep -u "$U" -n -x gtk4-print-edit)
    grep -E "fwlinux-open/note.txt.*= [0-9]+$" "$TR" >/dev/null && ok "it opens the file passed by Open with" || bad "the passed file was not opened: $(grep note.txt "$TR" | head -1)"
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -f "$APPS/fwgtkdoc.desktop" "$APPS/fwgtkopen.desktop" "$TR"
fi
rm -f "$APPS/fwprobe.desktop" "$APPS/fwgtk.desktop" ~/Documents/fwlinux-secret.txt "$OUT"
rm -rf ~/fwlinux-open ~/.config/fwprobe ~/.cache/fwprobe
fwlinux_done
