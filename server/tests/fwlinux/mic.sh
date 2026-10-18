#!/bin/bash
# mic.sh — "no microphone" for Linux apps inside PipeWire (src/sec/pwguard.c): the restricted
# app's capture streams are closed and the capture nodes hidden from it, live. Runs the real guard
# (build/pwguard-cli) against the session's PipeWire and records from inside an app scope with
# pw-record (native PipeWire) and parecord (PulseAudio API), and arecord (ALSA → PipeWire) if
# present. Needs an Audio/Source node (any, even a virtual sound card's).
#
#   bash server/tests/fwlinux/mic.sh
. "$(dirname "$0")/lib.sh"
D=$(cd "$(dirname "$0")" && pwd)
CLI=$D/../../build/pwguard-cli
[ -x "$CLI" ] || { echo "missing build/pwguard-cli (make -C server test)"; exit 2; }
export XDG_RUNTIME_DIR=/run/user/$U
[ -S "$XDG_RUNTIME_DIR/pipewire-0-manager" ] || { echo "skipped: needs the session's PipeWire"; exit 0; }
APP=desktop:fwmic
SC=$(scope fwmic)
OUTD=$(mktemp -d); chmod 755 "$OUTD"
GLOG=$OUTD/guard.log
restrict $APP ""
"$CLI" 60 > "$GLOG" 2>&1 &
GPID=$!
sleep 1
# rec <tool> <seconds>: records inside the scope; prints the bytes of audio it got
rec() {
    local f=$OUTD/$1.raw
    rm -f "$f"
    case $1 in
        pw-record) in_scope "$SC" timeout "$2" pw-record --rate 48000 --channels 1 --format s16 "$f" >/dev/null 2>&1 ;;
        parecord) in_scope "$SC" timeout "$2" parecord --raw --rate=48000 --channels=1 --format=s16le "$f" >/dev/null 2>&1 ;;
        arecord) in_scope "$SC" timeout "$2" arecord -q -D pipewire -f S16_LE -r 48000 -c 1 -t raw "$f" >/dev/null 2>&1 ;;
    esac
    stat -c %s "$f" 2>/dev/null || echo 0
}
TOOLS="pw-record parecord"
command -v arecord >/dev/null && [ -e /usr/share/alsa/alsa.conf.d/50-pipewire.conf ] && TOOLS="$TOOLS arecord"

echo "1. not restricted: each tool records"
for t in $TOOLS; do n=$(rec $t 3); [ "$n" -gt 20000 ] && ok "$t recorded $n bytes" || bad "$t recorded only $n bytes"; done

echo "2. no-mic: nothing is recorded"
restrict $APP no-mic
sleep 1.5                               # the guard rereads the rules every second
for t in $TOOLS; do n=$(rec $t 3); [ "$n" -lt 4096 ] && ok "$t got $n bytes" || bad "$t still recorded $n bytes"; done
grep -q "fwmic is restricted (no-mic)" "$GLOG" && ok "the guard closed the capture stream(s)" || bad "no closing in the guard's log"
seen=$(in_scope "$SC" pw-dump 2>/dev/null | grep -c '"media.class": "Audio/Source"')
all=$(pw-dump 2>/dev/null | grep -c '"media.class": "Audio/Source"')
[ "$seen" = 0 ] && [ "$all" -gt 0 ] && ok "the app sees no microphone node ($all exist)" || bad "the app sees $seen of $all microphone nodes"
python3 -c "import wave,sys; w=wave.open(sys.argv[1],'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000); w.writeframes(bytes(96000)); w.close()" "$OUTD/silence.wav"; chmod 644 "$OUTD/silence.wav"
in_scope "$SC" timeout 5 pw-play "$OUTD/silence.wav" >/dev/null 2>&1 && ok "playback still works for the app" || bad "playback failed"

echo "3. live: a recording that is running is cut when the restriction is set"
restrict $APP ""
sleep 1.5
f=$OUTD/live.raw
in_scope "$SC" timeout 8 pw-record --rate 48000 --channels 1 --format s16 "$f" >/dev/null 2>&1 &
RP=$!
sleep 2.5
before=$(stat -c %s "$f" 2>/dev/null || echo 0)
restrict $APP no-mic
sleep 3
mid=$(stat -c %s "$f" 2>/dev/null || echo 0)
sleep 2
after=$(stat -c %s "$f" 2>/dev/null || echo 0)
wait $RP 2>/dev/null
[ "$before" -gt 20000 ] && ok "it was recording ($before bytes)" || bad "it wasn't recording ($before)"
[ "$after" = "$mid" ] && ok "it stopped growing once restricted ($mid → $after)" || bad "it kept recording ($mid → $after)"

echo "4. lifted: records again"
restrict $APP ""
sleep 1.5
n=$(rec pw-record 3); [ "$n" -gt 20000 ] && ok "pw-record recorded $n bytes" || bad "pw-record recorded only $n bytes"

echo "5. outside the app's scope while it is restricted"
restrict $APP no-mic
sleep 1.5
f=$OUTD/out.raw; timeout 3 pw-record --rate 48000 --channels 1 --format s16 "$f" >/dev/null 2>&1
n=$(stat -c %s "$f" 2>/dev/null || echo 0)
[ "$n" -gt 20000 ] && ok "another program records ($n bytes)" || bad "another program got $n bytes"
kill $GPID 2>/dev/null; wait $GPID 2>/dev/null
rm -rf "$OUTD"
fwlinux_done
