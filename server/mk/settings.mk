# Settings' hardware pages (src/hw, docs/SETTINGS.md): displays (EDID, modes, Night Light),
# Bluetooth (BlueZ over our own D-Bus client), battery and power, date and time, default apps.
# Objects are added to lord-server as extra prerequisites ($^ in its link rule takes them).
HW_SRC := src/hw/edid.c src/hw/modes.c src/hw/night.c src/hw/mimeapps.c src/hw/power.c src/hw/jbuf.c src/hw/display.c \
          src/hw/uevent.c src/hw/sysbus.c src/hw/bt.c src/hw/ppd.c src/hw/hw.c src/hw/hw_admin.c src/hw/defaults.c src/hw/notifyd.c src/ui/js_hw.c \
          src/ui/js_fan.c src/fan/fancurve.c
HW_HDR := $(wildcard src/hw/*.h) $(wildcard src/fan/*.h) src/ui/js_hw.h src/ui/js_fan.h src/kms.h src/vault/dbus.h
HW_OBJ := $(HW_SRC:src/%.c=build/obj/%.o)
$(HW_OBJ): $(HW_HDR)
build/lord-server: $(HW_OBJ)
# pure parts: EDID, mode selection, gamma/temperature/sun math, mimeapps.list and globs2, power_supply,
# and the admin actions' validators
test: build/settings-hw-test
build/settings-hw-test: tests/settings_hw_test.c src/hw/edid.c src/hw/modes.c src/hw/night.c src/hw/mimeapps.c src/hw/power.c src/hw/hw_admin.c src/sec/json.c $(HW_HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/settings_hw_test.c src/hw/edid.c src/hw/modes.c src/hw/night.c src/hw/mimeapps.c src/hw/power.c src/hw/hw_admin.c src/sec/json.c -lm
	./$@ || { rm -f $@; exit 1; }
# BlueZ parsing (GetManagedObjects, PropertiesChanged, Battery1, paths, state JSON) without a bus
test: build/bt-test
BT_TEST_SRC := tests/bt_test.c src/hw/bt.c src/hw/sysbus.c src/hw/jbuf.c src/vault/dbus.c src/sec/json.c
build/bt-test: $(BT_TEST_SRC) $(HW_HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ $(BT_TEST_SRC)
	./$@ || { rm -f $@; exit 1; }
