# lord-server — libc + headers del kernel + libxkbcommon + libwayland-server + QuickJS y stb vendorizados.
# Sin libdrm, libinput, X11 ni WebKit; el compositor Wayland (src/wl) y la terminal (src/term) son propios.
CC      ?= cc
CFLAGS  ?= -O2 -g
WARN    := -Wall -Wextra
CPPFLAGS += -D_GNU_SOURCE -Isrc -Ibuild/gen -isystem ../third_party/quickjs -isystem ../third_party/stb
LDLIBS  += -lm -lpthread -lxkbcommon -lwayland-server -lwayland-client -lcrypt

# Arquitectura: x86_64 (PC) o aarch64 (Raspberry Pi 5/4 y otras ARM64, ver docs/RASPBERRY.md). Nunca
# -march=native: el .deb tiene que andar en cualquier CPU de la arquitectura. En aarch64 NEON es
# parte de ARMv8-A (no hace falta -mfpu) y se usan los bucles NEON de canvas.c (NEON=0 los apaga;
# tests/canvas_test.c los compara con los de C).
ARCH ?= $(shell $(CC) -dumpmachine 2>/dev/null | cut -d- -f1)
ifeq ($(ARCH),aarch64)
NEON ?= 1
ifeq ($(NEON),1)
CPPFLAGS += -DLORD_NEON
endif
endif

# Protocolos Wayland: wayland-scanner genera el código desde los XML de wayland-protocols.
WLPROTO := /usr/share/wayland-protocols
WL_XML  := $(WLPROTO)/stable/xdg-shell/xdg-shell.xml $(WLPROTO)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
WL_GEN  := build/gen/xdg-shell-protocol.h build/gen/xdg-decoration-unstable-v1-protocol.h
WL_OBJ  := build/gen/xdg-shell-protocol.o build/gen/xdg-decoration-unstable-v1-protocol.o
# dmabuf, presentación, viewporter y mouse para juegos (M7/GPU): también sirven en el camino por CPU
WL_MORE := stable/linux-dmabuf/linux-dmabuf-v1 stable/presentation-time/presentation-time stable/viewporter/viewporter \
           unstable/relative-pointer/relative-pointer-unstable-v1 unstable/pointer-constraints/pointer-constraints-unstable-v1 \
           unstable/pointer-gestures/pointer-gestures-unstable-v1 \
           staging/cursor-shape/cursor-shape-v1 unstable/tablet/tablet-unstable-v2
WL_GEN  += $(foreach p,$(WL_MORE),build/gen/$(notdir $(p))-protocol.h)
WL_OBJ  += $(foreach p,$(WL_MORE),build/gen/$(notdir $(p))-protocol.o)

# Composición por GPU (src/gpu): Mesa (GBM + EGL + GLES2) si están sus -dev; si no (o con
# GPU=0) se compila sin GPU y lord-server compone siempre por CPU. Ver docs/GPU.md.
GPU ?= $(shell pkg-config --exists gbm egl glesv2 2>/dev/null && echo 1 || echo 0)
ifeq ($(GPU),1)
CPPFLAGS += -DLORD_GPU $(shell pkg-config --cflags gbm egl glesv2)
LDLIBS   += $(shell pkg-config --libs gbm egl glesv2)
endif

# Cabeceras de cliente Wayland para el modo app (ver CLIENT_PROTO; las interfaces las define el servidor)
APP_CLIENT_HDR := build/gen/client/xdg-shell-client-protocol.h build/gen/client/xdg-decoration-unstable-v1-client-protocol.h \
                  build/gen/client/pointer-gestures-unstable-v1-client-protocol.h
QJS_DIR := ../third_party/quickjs
QJS_OBJ := $(patsubst %,build/qjs/%.o,quickjs libregexp libunicode cutils dtoa)

# Aislamiento de apps .lpk, permisos y firma (src/sec, ver docs/PRIVACIDAD.md y docs/FIRMA.md)
# Monitor: métricas, conexiones y país de una IP (src/monitor, ver docs/MONITOR.md)
MON_SRC := src/monitor/mon.c src/monitor/conn.c src/monitor/mmdb.c src/monitor/map.c src/ui/js_monitor.c
# Medios (src/media, docs/MEDIOS.md): solo el lado de lord-server. FFmpeg lo enlaza solo lord-media.
MEDIA_SRC := src/media/media.c src/media/mview.c src/media/capture.c src/media/annot.c src/media/maudio.c src/media/mpulse.c src/media/proto.c src/ui/js_media.c
SEC_SRC := src/sec/json.c src/sec/sign.c src/sec/pkgstate.c src/sec/perms.c src/sec/cgroup.c src/sec/sandbox.c src/sec/brokersrv.c
# Modo app: lord-server --app <id> corre una app .lpk como cliente Wayland aislado (src/app)
APP_SRC := src/app/appclient.c src/app/appspawn.c src/ui/js_broker.c
SRC := src/main.c src/kms.c src/gpu/gpu.c src/input.c src/canvas.c src/admin.c src/install/install.c src/apps.c src/lpk.c src/fsops.c src/remote/mirror.c src/remote/remote_admin.c src/users/users_admin.c src/users/homedirs.c src/keyboard.c src/ime.c src/wl/wl.c \
       src/ui/node.c src/ui/style.c src/ui/layout.c src/ui/text.c src/ui/image.c \
       src/ui/paint.c src/ui/js.c src/ui/js_sys.c src/ui/js_term.c src/ui/js_wasm.c src/wasm/wasm.c src/wasm/wasm_run.c src/ui/js_sound.c src/ui/js_net.c src/sound/snd.c src/sound/pulse.c src/ui/otf.c src/ui/fonts.c \
       src/ui/unicode.c src/ui/bidi.c src/ui/shape.c src/ui/shape_indic.c src/ui/shape_syl.c src/ui/shape_khmer.c src/ui/shape_myanmar.c src/ui/shape_use.c src/ui/thbrk.c src/ui/textlayout.c \
       src/term/vt.c src/term/pty.c src/term/term.c src/term/term_paint.c src/term/hold.c \
       src/net/nl.c src/net/crypto.c src/net/state.c src/net/sys.c src/net/dhcp.c src/net/wifi.c src/net/wg.c src/net/ovpn.c src/net/vpn.c src/net/vici.c src/net/ipsec.c src/net/vpnproc.c src/net/actions.c \
       src/pkg/crypto.c src/pkg/pgp.c src/pkg/xz.c src/pkg/deb.c src/pkg/idx.c src/pkg/status.c src/pkg/resolve.c src/pkg/http.c src/pkg/pkg.c src/ui/js_pkg.c src/ui/js_sec.c \
       $(SEC_SRC) $(APP_SRC) $(MON_SRC) $(MEDIA_SRC)
# Documentos e impresión (M9): sesiones del ayudante lord-doc y cliente IPP/mDNS/eSCL propios
# (src/doc, src/print; ver docs/DOCUMENTOS.md y docs/IMPRESION.md). Sin Poppler ni libarchive acá.
DOC_SRC := src/doc/doc.c src/doc/wire.c src/ui/js_doc.c src/print/ipp.c src/print/phttp.c src/print/mdns.c src/print/cups.c \
           src/print/escl.c src/print/print.c src/ui/js_print.c
SRC += $(DOC_SRC)
# Firewall por app (src/fw, docs/FIREWALL.md): reglas, decisión y nombres aprendidos del DNS
FW_SRC := src/fw/rules.c src/fw/dnsmap.c src/fw/fwmap.c src/ui/js_fw.c
SRC += $(FW_SRC)
OBJ := $(SRC:src/%.c=build/obj/%.o)
HDR := $(wildcard src/*.h src/gpu/*.h src/ui/*.h src/term/*.h src/wl/*.h src/wasm/*.h src/sound/*.h src/pkg/*.h src/net/*.h src/install/*.h src/remote/*.h src/users/*.h src/sec/*.h src/monitor/*.h src/media/*.h src/app/*.h src/doc/*.h src/print/*.h) $(WL_GEN) $(APP_CLIENT_HDR)
HDR += $(wildcard src/fw/*.h)
# Touchpad y pantallas táctiles: driver propio (src/tp, sin libinput), gestos y toques para las apps
# Wayland (src/wl/wl_input.c) y lord.input (src/ui/js_input.c). Ver docs/TRACKPAD.md.
TP_SRC := src/tp/tp.c src/tp/tp_route.c src/wl/wl_input.c src/ui/js_input.c
SRC += $(TP_SRC)
# Ventanas: arrastre en C, capa de la ventana que se mueve y cursores propios (docs/VENTANAS.md).
# cursor-shape-v1 necesita las interfaces de tablet-v2 (solo se enlazan, no se anuncian).
# Apps GTK/GNOME/Qt: org.freedesktop.portal.Settings propio (src/ui/portal.c) con el cliente D-Bus de
# Contraseñas (src/vault/dbus.c, docs/CONTRASENAS.md).
WM_SRC := src/cursor.c src/ui/grab.c src/ui/js_wm.c src/ui/portal.c src/vault/dbus.c
SRC += $(WM_SRC)
# Reloj y Ayuda (docs/SISTEMA.md): zonas horarias con un lector TZif propio y los artículos de ayuda
CLOCK_SRC := src/ui/tzif.c src/ui/js_help.c
SRC += $(CLOCK_SRC)
# Registros (docs/SISTEMA.md): lector propio del journal de systemd, la acción logs-system y lord.logs
LOGS_SRC := src/logs/journal.c src/logs/logs_admin.c src/ui/js_logs.c
SRC += $(LOGS_SRC)
HDR += $(wildcard src/logs/*.h)
# Copias de seguridad (docs/SISTEMA.md): instantáneas con enlaces duros en un hilo de baja prioridad
BACKUP_SRC := src/backup/engine.c src/backup/crypt.c src/backup/backup.c src/ui/js_backup.c
SRC += $(BACKUP_SRC)
HDR += $(wildcard src/backup/*.h)
# el cifrado opcional usa Monocypher (vendorizado, third_party/monocypher; su objeto es el de mk/vault.mk)
build/obj/backup/%.o: CPPFLAGS += -isystem ../third_party/monocypher
# Discos (docs/SISTEMA.md): discos, particiones y montajes sin root, SMART propio y las acciones disk-*
DISK_SRC := src/disk/disk.c src/disk/smart.c src/disk/disk_admin.c src/ui/js_disk.c
SRC += $(DISK_SRC)
HDR += $(wildcard src/disk/*.h)
OBJ := $(SRC:src/%.c=build/obj/%.o)
HDR += $(wildcard src/tp/*.h)

.PHONY: all clean test
# lord-sessiond: sesiones, pantalla de ingreso y escritorio remoto (docs/SESIONES.md). Es el
# único que enlaza OpenSSL (TLS); si no está libssl-dev, se compila solo lord-server.
SESSIOND ?= $(shell pkg-config --exists openssl 2>/dev/null && echo 1 || echo 0)
ALL := build/lord-server
ifeq ($(SESSIOND),1)
ALL += build/lord-sessiond
endif
# lord-media: el ayudante de medios (docs/MEDIOS.md). Es el único que enlaza FFmpeg (y libva);
# lord-server nunca. Si faltan los -dev de FFmpeg, se compila todo lo demás sin él.
MEDIA ?= $(shell pkg-config --exists libavcodec libavformat libavutil libswscale libswresample 2>/dev/null && echo 1 || echo 0)
ifeq ($(MEDIA),1)
ALL += build/lord-media
endif
# lord-doc: el ayudante de documentos (docs/DOCUMENTOS.md). Es el único que enlaza Poppler y
# libarchive (y cairo, FreeType y fontconfig, que vienen con Poppler); lord-server nunca.
DOCHELPER ?= $(shell pkg-config --exists poppler-glib libarchive cairo-pdf cairo-ft freetype2 fontconfig 2>/dev/null && echo 1 || echo 0)
ifeq ($(DOCHELPER),1)
ALL += build/lord-doc
endif
# lord-netperm (src/netperm, docs/FIREWALL.md paso 2): el servicio root que aplica los permisos de
# red por app con programas BPF de cgroup. Los programas se compilan con clang -target bpf (BPF=1,
# si hay clang) y van adentro del binario (build/bpf2c → build/gen/np_bpf.h). Sin clang se compila
# igual y el servicio avisa que no tiene BPF. Sin libbpf ni bcc.
.DEFAULT_GOAL := all
BPF ?= $(shell command -v clang >/dev/null 2>&1 && echo 1 || echo 0)
NP_SRC := src/netperm/netperm.c src/netperm/bpfsys.c src/netperm/npcompile.c src/netperm/npdns.c \
          src/fw/rules.c src/fw/dnsmap.c src/fw/fwmap.c src/sec/json.c
ALL += build/lord-netperm
ifeq ($(BPF),1)
NP_GEN := build/gen/np_bpf.h
NP_CPP := -DNP_HAVE_BPF
endif
build/bpf2c: src/netperm/bpf2c.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(WARN) -std=c11 -o $@ $<
build/np.bpf.o: src/netperm/np.bpf.c src/netperm/np_shared.h
	@mkdir -p build
	clang -target bpf -O2 -Wall -Wextra -Isrc -I/usr/include/$(shell $(CC) -dumpmachine) -c -o $@ $<
build/gen/np_bpf.h: build/np.bpf.o build/bpf2c
	@mkdir -p build/gen
	build/bpf2c $< $@
build/lord-netperm: $(NP_SRC) $(wildcard src/netperm/*.h) $(wildcard src/fw/*.h) src/sec/json.h $(NP_GEN)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(NP_CPP) $(CFLAGS) $(WARN) -std=c11 -o $@ $(NP_SRC)
test: build/npcompile-test
build/npcompile-test: tests/npcompile_test.c src/netperm/npcompile.c src/netperm/npdns.c src/fw/rules.c src/fw/dnsmap.c src/fw/fwmap.c src/sec/json.c $(wildcard src/netperm/*.h) $(wildcard src/fw/*.h)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/npcompile_test.c src/netperm/npcompile.c src/netperm/npdns.c src/fw/rules.c src/fw/dnsmap.c src/fw/fwmap.c src/sec/json.c
	./$@ || { rm -f $@; exit 1; }
all: $(ALL)

# Pruebas sin pantalla: emulador de terminal (tests/vt_dump.c), armado OpenType contra
# HarfBuzz (tests/shape_test.c), bidi contra BidiCharacterTest.txt (tests/bidi_test.c) y
# métodos de entrada (tests/ime_test.c); cortes del tailandés (tests/thbrk_test.c, contra libthai
# con tests/thbrk_compare.sh).
test: build/vt-dump build/shape-test build/bidi-test build/ime-test build/thbrk-test build/sound-test build/wasm-test build/net-test build/wasm-js-test build/pkg-test build/fsops-test build/sign-test build/mmdb-test build/canvas-test build/arcpath-test build/ipp-test build/tp-test build/media-test
SHAPE_SRC := src/ui/otf.c src/ui/shape.c src/ui/shape_indic.c src/ui/shape_syl.c src/ui/shape_khmer.c src/ui/shape_myanmar.c src/ui/shape_use.c src/ui/unicode.c
build/shape-test: tests/shape_test.c $(SHAPE_SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/shape_test.c $(SHAPE_SRC)
WASM_SRC := src/wasm/wasm.c src/wasm/wasm_run.c
build/wasm-js-test: tests/wasm_js_test.c src/ui/js_wasm.c src/ui/js_wasm.h $(WASM_SRC) src/wasm/wasm.h $(QJS_OBJ)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/wasm_js_test.c src/ui/js_wasm.c $(WASM_SRC) $(QJS_OBJ) -lm -lpthread
build/wasm-test: tests/wasm_test.c $(WASM_SRC) src/wasm/wasm.h src/wasm/wasm_int.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/wasm_test.c $(WASM_SRC) -lm
PKG_SRC := src/pkg/crypto.c src/pkg/pgp.c src/pkg/xz.c src/pkg/deb.c src/pkg/idx.c src/pkg/status.c src/pkg/resolve.c src/pkg/http.c src/pkg/pkg.c
build/pkg-test: tests/pkg_test.c $(PKG_SRC) build/obj/ui/stb.o src/admin.h $(wildcard src/pkg/*.h)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/pkg_test.c $(PKG_SRC) build/obj/ui/stb.o -lm
PKG_READ_SRC := src/pkg/deb.c src/pkg/idx.c src/pkg/status.c src/pkg/resolve.c src/pkg/crypto.c
build/pkg-js-test: tests/pkg_js_test.c src/ui/js_pkg.c $(PKG_READ_SRC) $(QJS_OBJ) $(wildcard src/pkg/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/pkg_js_test.c src/ui/js_pkg.c $(PKG_READ_SRC) $(QJS_OBJ) -lm -lpthread
build/thbrk-test: tests/thbrk_test.c src/ui/thbrk.c src/ui/thbrk.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/thbrk_test.c src/ui/thbrk.c
# firma de paquetes: SHA-256 (NIST), Ed25519 (RFC 8032), JSON, contenido.json y catálogo
build/pkgstate-test: tests/pkgstate_test.c src/sec/pkgstate.c src/sec/sign.c src/sec/json.c src/lpk.c src/pkg/crypto.c build/obj/ui/stb.o $(wildcard src/sec/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/pkgstate_test.c src/sec/pkgstate.c src/sec/sign.c src/sec/json.c src/lpk.c src/pkg/crypto.c build/obj/ui/stb.o -lm
build/sandbox-test: tests/sandbox_test.c src/sec/sandbox.c src/sec/cgroup.c src/sec/sandbox.h src/sec/cgroup.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -D_GNU_SOURCE -o $@ tests/sandbox_test.c src/sec/sandbox.c src/sec/cgroup.c -lpthread
build/sign-test: tests/sign_test.c src/sec/json.c src/sec/sign.c src/lpk.c src/pkg/crypto.c build/obj/ui/stb.o $(wildcard src/sec/*.h) src/lpk.h src/pkg/crypto.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -DLORD_SIGN_TEST -o $@ tests/sign_test.c src/sec/json.c src/sec/sign.c src/lpk.c src/pkg/crypto.c build/obj/ui/stb.o -lm
# zonas horarias del Reloj: TZif y reglas POSIX contra glibc (docs/SISTEMA.md)
test: build/tzif-test
build/tzif-test: tests/tzif_test.c src/ui/tzif.c src/ui/tzif.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/tzif_test.c src/ui/tzif.c
	./$@ || { rm -f $@; exit 1; }
# lector del journal (Registros): archivos sintéticos normales y compactos, LZ4 y XZ, archivos
# rotos y cortados; `build/journal-test dump <archivo>` vuelca JSON para comparar con journalctl
test: build/journal-test
build/journal-test: tests/journal_test.c src/logs/journal.c src/logs/journal.h src/pkg/xz.c build/obj/ui/stb.o
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/journal_test.c src/logs/journal.c src/pkg/xz.c build/obj/ui/stb.o -lm
	./$@ || { rm -f $@; exit 1; }
# firewall por app: reglas (precedencia, redes, JSON) y nombres del DNS (paquetes rotos, LRU)
test: build/fw-rules-test build/dnsmap-test
build/fw-rules-test: tests/fw_rules_test.c src/fw/rules.c src/fw/fwmap.c src/sec/json.c $(wildcard src/fw/*.h) src/sec/json.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/fw_rules_test.c src/fw/rules.c src/fw/fwmap.c src/sec/json.c
	./$@ || { rm -f $@; exit 1; }
build/dnsmap-test: tests/dnsmap_test.c src/fw/dnsmap.c src/fw/rules.c src/fw/fwmap.c src/sec/json.c $(wildcard src/fw/*.h) src/sec/json.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/dnsmap_test.c src/fw/dnsmap.c src/fw/rules.c src/fw/fwmap.c src/sec/json.c
	./$@ || { rm -f $@; exit 1; }
# motor de Copias de seguridad: instantánea, enlaces duros, restaurar, retención, cortes, exclusiones
build/lord-server: build/obj/vault/monocypher.o
test: build/backup-test
build/backup-test: tests/backup_test.c src/backup/engine.c src/backup/crypt.c src/fsops.c src/backup/engine.h src/backup/crypt.h src/fsops.h build/obj/vault/monocypher.o
	@mkdir -p build
	$(CC) $(CPPFLAGS) -isystem ../third_party/monocypher $(CFLAGS) $(WARN) -std=c11 -o $@ tests/backup_test.c src/backup/engine.c src/backup/crypt.c src/fsops.c build/obj/vault/monocypher.o -lpthread
	./$@ || { rm -f $@; exit 1; }
# lector de MaxMind DB del Monitor (país de una IP): búsquedas conocidas y archivos rotos
build/mmdb-test: tests/mmdb_test.c src/monitor/mmdb.c src/monitor/mmdb.h src/pkg/xz.c build/obj/ui/stb.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/mmdb_test.c src/monitor/mmdb.c src/pkg/xz.c build/obj/ui/stb.o -lm
# tiempo de dibujo del mapa vectorial del Monitor (no es una prueba: mide; ver docs/MONITOR.md)
build/map-bench: tests/map_bench.c src/monitor/map.c src/monitor/map.h src/monitor/mon.c src/monitor/conn.c src/monitor/mmdb.c src/pkg/xz.c build/obj/ui/stb.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/map_bench.c src/monitor/map.c src/monitor/mon.c src/monitor/conn.c src/monitor/mmdb.c src/pkg/xz.c build/obj/ui/stb.o -lm -lpthread
# filas de canvas.c: SIMD (NEON en aarch64) contra C, bit a bit, y cuánto tarda cada una
build/canvas-test: tests/canvas_test.c src/canvas.c src/canvas.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -DLORD_CANVAS_TEST -o $@ tests/canvas_test.c src/canvas.c -lm
build/fsops-test: tests/fsops_test.c src/fsops.c src/fsops.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/fsops_test.c src/fsops.c -lpthread
# driver de touchpad (src/tp): gestos del touchpad simulado (../tests/touchpad-sim) → lo reconocido
build/tp-test: tests/tp_test.c src/tp/tp.c src/tp/tp.h ../tests/touchpad-sim/gestures.c ../tests/touchpad-sim/gestures.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/tp_test.c src/tp/tp.c ../tests/touchpad-sim/gestures.c -lm
build/ime-test: tests/ime_test.c src/ime.c src/ime.h src/keyboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/ime_test.c src/ime.c
build/bidi-test: tests/bidi_test.c src/ui/bidi.c src/ui/unicode.c $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/bidi_test.c src/ui/bidi.c src/ui/unicode.c
build/sound-test: tests/sound_test.c src/sound/pulse.c src/sound/pulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/sound_test.c src/sound/pulse.c
build/sound-cli: tests/sound_cli.c src/sound/snd.c src/sound/pulse.c src/sound/snd.h src/sound/pulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/sound_cli.c src/sound/snd.c src/sound/pulse.c -lm
NET_SRC := src/net/nl.c src/net/crypto.c src/net/state.c src/net/sys.c src/net/dhcp.c src/net/wifi.c src/net/wg.c src/net/ovpn.c src/net/vpn.c src/net/vici.c src/net/ipsec.c src/net/vpnproc.c src/net/actions.c
build/net-test: tests/net_test.c $(NET_SRC) $(PKG_SRC) build/obj/ui/stb.o src/net/net.h src/net/netpriv.h src/net/vpn.h src/admin.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/net_test.c $(NET_SRC) $(PKG_SRC) build/obj/ui/stb.o -lm
# App Wayland de prueba con dmabuf (udmabuf): ver docs/GPU.md. Necesita libwayland-client.
CLIENT_PROTO := stable/xdg-shell/xdg-shell unstable/xdg-decoration/xdg-decoration-unstable-v1 stable/linux-dmabuf/linux-dmabuf-v1 stable/presentation-time/presentation-time \
                unstable/relative-pointer/relative-pointer-unstable-v1 unstable/pointer-constraints/pointer-constraints-unstable-v1 \
                unstable/pointer-gestures/pointer-gestures-unstable-v1
CLIENT_GEN := $(foreach p,$(CLIENT_PROTO),build/gen/client/$(notdir $(p))-client-protocol.h build/gen/client/$(notdir $(p))-protocol.c)
define wl_client_proto
build/gen/client/$(notdir $(1))-client-protocol.h: $(WLPROTO)/$(1).xml
	@mkdir -p build/gen/client
	wayland-scanner client-header $$< $$@
build/gen/client/$(notdir $(1))-protocol.c: $(WLPROTO)/$(1).xml
	@mkdir -p build/gen/client
	wayland-scanner private-code $$< $$@
endef
$(foreach p,$(CLIENT_PROTO),$(eval $(call wl_client_proto,$(p))))
build/dmabuf-client: tests/dmabuf_client.c $(CLIENT_GEN)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -Ibuild/gen $(WARN) -std=c11 -o $@ tests/dmabuf_client.c $(filter %.c,$(CLIENT_GEN)) -lwayland-client

MEDIA_PKGS := libavcodec libavformat libavutil libswscale libswresample
MEDIA_VA := $(shell pkg-config --exists libva libva-drm 2>/dev/null && echo 1 || echo 0)
ifeq ($(MEDIA_VA),1)
MEDIA_PKGS += libva libva-drm
MEDIA_CPPFLAGS := -DLM_VAAPI
endif
LM_SRC := $(wildcard src/media/helper/*.c) src/media/proto.c src/sec/sandbox.c src/sec/cgroup.c
build/lord-media: $(LM_SRC) $(wildcard src/media/helper/*.h) src/media/proto.h src/media/aring.h src/sec/sandbox.h src/sec/cgroup.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(MEDIA_CPPFLAGS) $(shell pkg-config --cflags $(MEDIA_PKGS)) $(CFLAGS) $(WARN) -std=c11 -o $@ $(LM_SRC) \
	    $(shell pkg-config --libs $(MEDIA_PKGS)) -lpthread -lm
MEDIA_TEST_SRC := tests/media_test.c src/media/proto.c src/media/maudio.c src/media/mpulse.c src/sound/pulse.c
build/media-test: $(MEDIA_TEST_SRC) src/media/proto.h src/media/aring.h src/media/media_int.h src/media/mpulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ $(MEDIA_TEST_SRC) -lm

build/vt-dump: tests/vt_dump.c build/obj/term/vt.o build/obj/ui/stb.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ $^ $(LDLIBS)

build/gen/%-protocol.h: $(WLPROTO)/stable/xdg-shell/%.xml
	@mkdir -p build/gen
	wayland-scanner server-header $< $@
build/gen/%-protocol.c: $(WLPROTO)/stable/xdg-shell/%.xml
	@mkdir -p build/gen
	wayland-scanner private-code $< $@
build/gen/xdg-decoration-unstable-v1-protocol.h: $(WLPROTO)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	@mkdir -p build/gen
	wayland-scanner server-header $< $@
build/gen/xdg-decoration-unstable-v1-protocol.c: $(WLPROTO)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	@mkdir -p build/gen
	wayland-scanner private-code $< $@
define wl_proto
build/gen/$(notdir $(1))-protocol.h: $(WLPROTO)/$(1).xml
	@mkdir -p build/gen
	wayland-scanner server-header $$< $$@
build/gen/$(notdir $(1))-protocol.c: $(WLPROTO)/$(1).xml
	@mkdir -p build/gen
	wayland-scanner private-code $$< $$@
endef
$(foreach p,$(WL_MORE),$(eval $(call wl_proto,$(p))))

build/gen/%.o: build/gen/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -c -o $@ $<

build/lord-server: $(OBJ) build/obj/ui/stb.o $(QJS_OBJ) $(WL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

SESSIOND_OBJ := build/obj/remote/sessiond.o build/obj/remote/sess.o build/obj/remote/monitor.o build/obj/remote/conn.o \
                build/obj/remote/enc.o build/obj/remote/pages.o build/obj/sec/cgroup.o build/obj/remote/netsup.o
build/obj/remote/pages.o: src/remote/login.html src/remote/client.html
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -z noexecstack -o $@ src/remote/login.html src/remote/client.html
build/lord-sessiond: $(SESSIOND_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs openssl) -lcrypt -lpthread

build/obj/%.o: src/%.c $(HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -c -o $@ $<

# Código de terceros: se compila tal cual, sin nuestros warnings.
build/obj/ui/stb.o: src/ui/stb.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -c -o $@ $<

build/qjs/%.o: $(QJS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -DCONFIG_VERSION=\"$$(cat $(QJS_DIR)/VERSION)\" -fwrapv -ffp-contract=off -w -c -o $@ $<

clean:
	rm -rf build

# ---- documentos (M9) ----
LD_SRC := src/doc/helper.c src/doc/h_pdf.c src/doc/h_arc.c src/doc/h_font.c src/doc/h_make.c src/doc/wire.c src/doc/arcpath.c \
          src/sec/json.c src/sec/sandbox.c src/sec/cgroup.c
LD_PKGS := poppler-glib libarchive cairo-pdf cairo-ft freetype2 fontconfig
build/obj/doc/h_stb.o: src/doc/h_stb.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -c -o $@ $<
build/lord-doc: $(LD_SRC) build/obj/doc/h_stb.o $(wildcard src/doc/*.h) src/sec/sandbox.h src/sec/cgroup.h src/sec/json.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 $(shell pkg-config --cflags $(LD_PKGS)) -o $@ $(LD_SRC) build/obj/doc/h_stb.o $(shell pkg-config --libs $(LD_PKGS)) -lm
# reglas para extraer sin salir de la carpeta (tests/arcpath_test.c)
build/arcpath-test: tests/arcpath_test.c src/doc/arcpath.c src/doc/arcpath.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/arcpath_test.c src/doc/arcpath.c
# codificador/decodificador IPP, mDNS y rangos de páginas (tests/ipp_test.c)
build/ipp-test: tests/ipp_test.c src/print/ipp.c src/print/mdns.c src/print/cups.c src/print/phttp.c src/doc/wire.c src/sec/json.c $(wildcard src/print/*.h)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/ipp_test.c src/print/ipp.c src/print/mdns.c src/print/cups.c src/print/phttp.c src/doc/wire.c src/sec/json.c
# lord-doc de punta a punta: comprimidos maliciosos, PDF y el encierro (necesita build/lord-doc)
build/doc-test: tests/doc_test.c src/doc/wire.c src/sec/json.c $(wildcard src/doc/*.h)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/doc_test.c src/doc/wire.c src/sec/json.c

# Contraseñas y llavero del sistema (docs/CONTRASENAS.md): reglas en mk/vault*.mk
include $(sort $(wildcard mk/*.mk))

# lord-netd (docs/RED.md, «La red es del sistema»): admin.c con LORD_NETD (solo las acciones net-*)
# más la red y el motor de paquetes (net-vpn-enable). Solo libc; lo lanza y vigila lord-sessiond.
all: build/lord-netd
NETD_OBJ := build/obj/netd/admin.o build/obj/netd/netd.o $(NET_SRC:src/%.c=build/obj/%.o) $(PKG_SRC:src/%.c=build/obj/%.o) build/obj/ui/stb.o
build/obj/netd/admin.o: src/admin.c $(HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DLORD_NETD $(CFLAGS) $(WARN) -std=c11 -c -o $@ $<
build/obj/netd/netd.o: src/net/netd.c src/admin.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -c -o $@ $<
build/lord-netd: $(NETD_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lm -lpthread

# analizadores de SMART (ATA y NVMe) de la app Discos: datos armados a mano, sano/advertencia/falla
test: build/smart-test
build/smart-test: tests/smart_test.c src/disk/smart.c src/disk/disk.h src/admin.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/smart_test.c src/disk/smart.c
	./$@ || { rm -f $@; exit 1; }
# prueba a mano de las acciones disk-* (root y dispositivos de verdad; no está en `test`)
build/disk-cli: tests/disk_cli.c src/disk/disk.c src/disk/smart.c src/disk/disk_admin.c src/pkg/crypto.c src/disk/disk.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/disk_cli.c src/disk/disk.c src/disk/smart.c src/disk/disk_admin.c src/pkg/crypto.c
# carpetas del usuario (src/users/homedirs.c): idiomas, user-dirs.dirs, no pisar, no seguir enlaces
test: build/homedirs-test
build/homedirs-test: tests/homedirs_test.c src/users/homedirs.c src/users/homedirs.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/homedirs_test.c src/users/homedirs.c
	./$@ || { rm -f $@; exit 1; }
