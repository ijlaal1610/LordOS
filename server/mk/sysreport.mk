# System Information (0.8.5, src/sysreport, docs/BENCHMARK.md): the collector, shared by the
# `lordos-sysreport` command and (later) lord.sysinfo. Read-only, no root, libc only.
SYSREPORT_SRC := src/sysreport/sysreport.c src/sysreport/sysreport_out.c src/sysreport/ids.c src/hw/edid.c src/hw/jbuf.c src/sec/json.c
SYSREPORT_HDR := $(wildcard src/sysreport/*.h) src/hw/edid.h src/hw/jbuf.h src/sec/json.h
all: build/lordos-sysreport
build/lordos-sysreport: src/sysreport/cli.c $(SYSREPORT_SRC) $(SYSREPORT_HDR) build/gen/lord_version.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) -Ibuild/gen $(CFLAGS) $(WARN) -std=c11 -o $@ src/sysreport/cli.c $(SYSREPORT_SRC)
# five fake machines full of serials, UUIDs, MACs and names that must not reach a report;
# `build/sysreport-test fixture <dir> amd|intel|nvidia|rpi5|vm` writes one for `--root`
test: build/sysreport-test
build/sysreport-test: tests/sysreport_test.c $(SYSREPORT_SRC) $(SYSREPORT_HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/sysreport_test.c $(SYSREPORT_SRC)
	./$@ || { rm -f $@; exit 1; }
