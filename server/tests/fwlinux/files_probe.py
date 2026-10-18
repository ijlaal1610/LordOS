#!/usr/bin/python3
# files_probe.py — stand-in app for tests/fwlinux/files.sh: launched by the real launcher
# (build/launch-cli) through a .desktop, it tries what a restricted app should and shouldn't be
# able to do and writes the results as JSON to /tmp/fwlinux-files-<uid>.json.
import json, os, sys
home = os.environ["HOME"]
res = {}
def t(name, fn):
    try:
        fn(); res[name] = "ok"
    except OSError as e:
        res[name] = os.strerror(e.errno) if e.errno else str(e)
def rd(p): open(p, "rb").read(1)
def wr(p): open(p, "ab").close()
def ls(p): os.listdir(p)
t("read_documents", lambda: rd(home + "/Documents/fwlinux-secret.txt"))
t("list_documents", lambda: ls(home + "/Documents"))
t("list_home", lambda: ls(home))
t("read_firewall_json", lambda: rd(home + "/.config/lordos/firewall.json"))
t("write_autostart", lambda: wr(home + "/.config/autostart/fwlinux.desktop"))
t("read_ssh", lambda: ls(home + "/.ssh"))
t("read_system", lambda: rd("/usr/share/applications/" + sorted(os.listdir("/usr/share/applications"))[0]))
t("write_tmp", lambda: wr("/tmp/fwlinux-probe-tmp"))
t("write_own_config", lambda: wr(home + "/.config/fwprobe/settings.ini"))
t("write_own_cache", lambda: wr(home + "/.cache/fwprobe/cache.bin"))
t("rename_in_own_config", lambda: (wr(home + "/.config/fwprobe/a.tmp"), os.rename(home + "/.config/fwprobe/a.tmp", home + "/.config/fwprobe/a")))
t("read_desktop_files", lambda: ls(home + "/.local/share/applications"))
t("write_runtime_dir", lambda: wr(os.environ.get("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid()) + "/fwlinux-probe"))
if len(sys.argv) > 1:
    t("read_opened_file", lambda: rd(sys.argv[1]))
    t("write_opened_file", lambda: wr(sys.argv[1]))
    t("read_next_to_opened", lambda: rd(os.path.join(os.path.dirname(sys.argv[1]), "fwlinux-sibling.txt")))
json.dump(res, open("/tmp/fwlinux-files-%d.json" % os.getuid(), "w"))
