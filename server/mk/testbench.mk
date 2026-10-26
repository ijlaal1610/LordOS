# lordos-testbench (src/testbench, docs/PAPER-VALIDATION.md): the visual test bench, a plain Wayland
# client (test card, frame and presentation timing, every key, the pointer, the compositor's globals).
# libwayland-client and libxkbcommon only; it reads no files.
TESTBENCH_SRC := src/testbench/testbench.c src/testbench/card.c src/testbench/draw.c
all: build/lordos-testbench
build/lordos-testbench: $(TESTBENCH_SRC) $(wildcard src/testbench/*.h) $(CLIENT_GEN)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Ibuild/gen $(WARN) -std=c11 -o $@ $(TESTBENCH_SRC) $(filter %.c,$(CLIENT_GEN)) -lwayland-client -lxkbcommon -lm
