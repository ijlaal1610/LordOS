# Códigos QR propios (src/vault/qr.c): respuestas conocidas (segno), ida y vuelta, corrección de
# errores, imágenes reales (tests/qr/) y fuzz. Con ASan/UBSan si el compilador los tiene.
QR_SAN := $(shell echo 'int main(void){return 0;}' | $(CC) -fsanitize=address,undefined -x c -o /dev/null - 2>/dev/null && echo -fsanitize=address,undefined)
build/qr-test: tests/qr_test.c src/vault/qr.c src/vault/qr.h src/vault/smem.c src/vault/smem.h tests/qr/qr_ref.h build/obj/vault/vstb.o
	@mkdir -p build
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(QR_SAN) $(WARN) -std=c11 -o $@ tests/qr_test.c src/vault/qr.c src/vault/smem.c build/obj/vault/vstb.o -lm -lpthread
	./$@ || { rm -f $@; exit 1; }
test: build/qr-test
