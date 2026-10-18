# Contraseñas y llavero del sistema (src/vault, docs/CONTRASENAS.md). Monocypher 4.0.2 vendorizado
# (CC0) en ../third_party/monocypher. Cada parte agrega sus pruebas en mk/vault-*.mk.
MC_DIR := ../third_party/monocypher
VAULT_CPP := -isystem $(MC_DIR)
build/obj/vault/monocypher.o: $(MC_DIR)/monocypher.c $(MC_DIR)/monocypher.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c -o $@ $<
# Vectores oficiales de Monocypher (tests/test.c + tests/vectors.h del tar de la versión)
build/monocypher-test: $(MC_DIR)/tests/test.c $(MC_DIR)/tests/utils.c $(MC_DIR)/tests/utils.h $(MC_DIR)/tests/vectors.h $(MC_DIR)/monocypher.c $(MC_DIR)/monocypher-ed25519.c
	@mkdir -p build
	$(CC) $(CFLAGS) -w -std=c11 -I$(MC_DIR) -I$(MC_DIR)/tests -o $@ $(MC_DIR)/tests/test.c $(MC_DIR)/tests/utils.c $(MC_DIR)/monocypher.c $(MC_DIR)/monocypher-ed25519.c
	./$@ > build/monocypher-test.log || { cat build/monocypher-test.log; rm -f $@; exit 1; }
	@tail -n 3 build/monocypher-test.log
test: build/monocypher-test
# Primitivas comunes de la bóveda (AES, SHA-1, HMAC, HKDF, arena de secretos, stb con la arena)
VAULT_BASE := src/vault/smem.c src/vault/vcrypto.c src/pkg/crypto.c
build/obj/vault/vstb.o: src/vault/vstb.c src/vault/vstb.h src/vault/smem.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -c -o $@ $<
build/vcrypto-test: tests/vcrypto_test.c $(VAULT_BASE) src/vault/vcrypto.h src/vault/smem.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/vcrypto_test.c $(VAULT_BASE) -lpthread
	./$@ || { rm -f $@; exit 1; }
test: build/vcrypto-test
# Listas embebidas en lord-vault (palabras de la EFF, contraseñas comunes): ../third_party/wordlists
build/obj/vault/lists.o: ../third_party/wordlists/eff_large_wordlist.txt ../third_party/wordlists/10k-most-common.txt
	@mkdir -p build/gen/vault build/obj/vault
	cp ../third_party/wordlists/eff_large_wordlist.txt build/gen/vault/words.txt
	cp ../third_party/wordlists/10k-most-common.txt build/gen/vault/common.txt
	cd build/gen/vault && $(LD) -r -b binary -z noexecstack -o ../../obj/vault/lists.o words.txt common.txt
VAULT_CORE := src/vault/smem.c src/vault/vcrypto.c src/vault/vjson.c src/vault/vfile.c src/vault/vmodel.c src/vault/vgen.c \
              src/vault/totp.c src/vault/vwire.c src/pkg/crypto.c
VAULT_HDR := $(wildcard src/vault/*.h) src/sec/sandbox.h
# formato, modelo, TOTP, generador, JSON y protocolo (tests/vault_test.c)
build/vault-test: tests/vault_test.c $(VAULT_CORE) build/obj/vault/monocypher.o build/obj/vault/lists.o $(VAULT_HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(VAULT_CPP) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/vault_test.c $(VAULT_CORE) build/obj/vault/monocypher.o build/obj/vault/lists.o -lpthread -lm
	./$@ || { rm -f $@; exit 1; }
test: build/vault-test
# lord-vault: el ayudante encerrado de Contraseñas y del llavero (src/vault/vault.c)
LV_SRC := $(VAULT_CORE) src/vault/vault.c src/vault/keyring.c src/vault/vimport.c src/vault/csvp.c src/vault/kdbx.c src/vault/xmlmini.c \
          src/vault/qr.c src/vault/ctap2.c src/vault/p256.c src/vault/cbor.c src/vault/secretsvc.c src/vault/dbus.c \
          src/sec/sandbox.c src/sec/cgroup.c
build/lord-vault: $(LV_SRC) build/obj/vault/monocypher.o build/obj/vault/vstb.o build/obj/vault/lists.o $(VAULT_HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(VAULT_CPP) $(CFLAGS) $(WARN) -std=c11 -o $@ $(LV_SRC) build/obj/vault/monocypher.o build/obj/vault/vstb.o build/obj/vault/lists.o -lpthread -lm
# (solo cuando están todas sus partes, así el árbol compila siempre)
LV_READY := $(and $(wildcard src/vault/secretsvc.c),$(wildcard src/vault/dbus.c),$(wildcard src/vault/vimport.c),$(wildcard src/vault/kdbx.c),$(wildcard src/vault/csvp.c),$(wildcard src/vault/xmlmini.c))
ifneq ($(LV_READY),)
all: build/lord-vault
endif
# Lado de lord-server: lanzar lord-vault, su canal y lord.vault (src/vault/vclient.c, src/ui/js_vault.c).
# Se suman a build/lord-server como prerrequisitos ($^ los incluye en el enlace).
VC_OBJ := build/obj/vault/vclient.o build/obj/vault/vwire.o build/obj/vault/fidodev.o build/obj/ui/js_vault.o
build/lord-server: $(VC_OBJ)
$(VC_OBJ): $(wildcard src/vault/vclient.h src/vault/vwire.h src/vault/fidodev.h src/ui/js_vault.h)
# lord-vault de punta a punta con el cliente de lord-server (necesita build/lord-vault; no corre solo)
build/vault-e2e: tests/vault_e2e.c src/vault/vclient.c src/vault/vwire.c src/vault/fidodev.c src/vault/vjson.c src/vault/smem.c $(VAULT_HDR) build/lord-vault
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/vault_e2e.c src/vault/vclient.c src/vault/vwire.c src/vault/fidodev.c src/vault/vjson.c src/vault/smem.c -lpthread
# bóveda de demostración para las capturas (no es una prueba)
build/vault-demo: tests/vault_demo.c src/vault/vclient.c src/vault/vwire.c src/vault/fidodev.c src/vault/vjson.c src/vault/smem.c $(VAULT_HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -std=c11 -o $@ tests/vault_demo.c src/vault/vclient.c src/vault/vwire.c src/vault/fidodev.c src/vault/vjson.c src/vault/smem.c -lpthread
