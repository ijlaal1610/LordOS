CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
.PHONY: all clean
all: build/lord-core
build/lord-core: core/main.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -rf build
