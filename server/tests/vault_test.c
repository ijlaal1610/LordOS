/* vault_test.c — la bóveda de Contraseñas (docs/CONTRASENAS.md):
 *   - formato .lvault: ida y vuelta, todas las formas de abrir, contraseña incorrecta, archivo
 *     alterado byte por byte (cabecera, ranuras, cuerpo, adjuntos, cola) y cortado en cada largo;
 *   - guardado atómico con copia .bak;
 *   - contenido (vmodel): crear/actualizar con "keep", historial, búsqueda en 10 000 entradas;
 *   - TOTP/HOTP contra RFC 6238/4226 y otpauth://;
 *   - generador y fuerza; JSON en la arena; protocolo (vwire) con mensajes grandes y fds;
 *   - tiempos: Argon2id calibrado para ~0,5–1 s. */
#include "vault/vfile.h"
#include "vault/vmodel.h"
#include "vault/vjson.h"
#include "vault/vwire.h"
#include "vault/totp.h"
#include "vault/vgen.h"
#include "vault/smem.h"
#include "vault/vcrypto.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern const char _binary_words_txt_start[], _binary_words_txt_end[];
extern const char _binary_common_txt_start[], _binary_common_txt_end[];

static int fails, total;
static void check(int ok, const char *what) { total++; if (!ok) { fails++; printf("FALLA: %s\n", what); } }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static uint8_t *file_bytes(int fd, size_t *len) {
    struct stat st;
    fstat(fd, &st);
    uint8_t *b = malloc((size_t)st.st_size + 1);
    *len = (size_t)pread(fd, b, (size_t)st.st_size, 0);
    return b;
}

static const char *PAYLOAD =
    "{\"v\":1,\"meta\":{\"autolock\":300},\"cats\":[{\"id\":\"g1\",\"name\":\"Bancos\"}],\"tags\":[],\"entries\":["
    "{\"id\":\"00112233445566778899aabbccddeeff\",\"cat\":\"internet\",\"title\":\"Correo de Ñandú\",\"fav\":true,"
    "\"tags\":[\"trabajo\"],\"f\":[{\"k\":\"username\",\"t\":\"text\",\"v\":\"ana@example.com\"},{\"k\":\"password\",\"t\":\"hidden\",\"v\":\"s3cr3t-\\\"q\\\"\"}]}]}";

/* arma una bóveda con varias ranuras en un archivo temporal; devuelve el fd */
static VKdf fast = { 8 * 1024, 1, 1 };
static uint8_t kf_hash[32], fido_out[32];
static char recovery[48];

static int make_vault(VHeader *h, uint8_t vk[32], const char *path, int with_att) {
    vf_header_new(h);
    vc_random(vk, 32);
    VFactors f = { .password = "correcta horse" };
    VSlot s;
    memset(&s, 0, sizeof s);
    check(vf_make_slot(h, &s, VS_PASSWORD, &f, vk, &fast, "principal") == VF_OK, "ranura de contraseña");
    h->slots[h->nslots++] = s;
    vf_keyfile_hash((const uint8_t *)"llave-de-prueba-con-mas-de-treinta-y-dos-bytes!!", 48, kf_hash);
    VFactors fk = { .keyfile = kf_hash };
    memset(&s, 0, sizeof s);
    check(vf_make_slot(h, &s, VS_KEYFILE, &fk, vk, &fast, "usb") == VF_OK, "ranura de archivo llave");
    h->slots[h->nslots++] = s;
    VFactors fp = { .password = "otra", .keyfile = kf_hash };
    memset(&s, 0, sizeof s);
    check(vf_make_slot(h, &s, VS_PASSKEY, &fp, vk, &fast, "2fa") == VF_OK, "ranura de contraseña + llave");
    h->slots[h->nslots++] = s;
    memset(fido_out, 0x5a, 32);
    VFactors ff = { .fido = fido_out };
    memset(&s, 0, sizeof s);
    s.cred_len = 3; s.cred[0] = 1; s.cred[1] = 2; s.cred[2] = 3; s.fido_pin = 1;
    check(vf_make_slot(h, &s, VS_FIDO2, &ff, vk, &fast, "YubiKey") == VF_OK, "ranura FIDO2");
    h->slots[h->nslots++] = s;
    vf_recovery_new(recovery);
    VFactors fr = { .recovery = recovery };
    memset(&s, 0, sizeof s);
    check(vf_make_slot(h, &s, VS_RECOVERY, &fr, vk, &fast, "") == VF_OK, "ranura de recuperación");
    h->slots[h->nslots++] = s;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    VAttOut att;
    uint8_t cipher[64];
    memset(&att, 0, sizeof att);
    if (with_att) {
        vc_random(att.id, 16);
        vf_att_seal(vk, h->vault_id, att.id, (const uint8_t *)"contenido del adjunto", 21, cipher, att.nonce, att.mac);
        att.cipher = cipher;
        att.len = 21;
    }
    check(vf_write_fd(fd, h, vk, (const uint8_t *)PAYLOAD, strlen(PAYLOAD), &att, with_att) == VF_OK, "escribir bóveda");
    return fd;
}

static void test_format(void) {
    VHeader h;
    uint8_t vk[32], vk2[32];
    int fd = make_vault(&h, vk, "/tmp/vault-test-pw-116.lvault", 1);
    size_t len;
    uint8_t *d = file_bytes(fd, &len);
    close(fd);
    VHeader r;
    check(vf_parse(d, len, &r) == VF_OK, "leer cabecera");
    check(r.nslots == 5 && r.natts == 1, "ranuras y adjuntos leídos");
    check(!strcmp(r.slots[3].label, "YubiKey") && r.slots[3].cred_len == 3 && r.slots[3].fido_pin == 1, "datos de la ranura FIDO2");
    int slot = -1;
    VFactors good = { .password = "correcta horse" }, bad = { .password = "incorrecta" };
    check(vf_open_slot(&r, 0, &bad, vk2, &slot) == VF_EBADKEY, "contraseña incorrecta → bad-key");
    check(vf_open_slot(&r, 0, &good, vk2, &slot) == VF_OK && slot == 0 && !memcmp(vk, vk2, 32), "contraseña correcta");
    VFactors fk = { .keyfile = kf_hash };
    check(vf_open_slot(&r, VS_KEYFILE, &fk, vk2, &slot) == VF_OK && slot == 1, "archivo llave");
    uint8_t other[32];
    vf_keyfile_hash((const uint8_t *)"otra llave distinta de treinta y dos bytes o mas....", 52, other);
    VFactors fk2 = { .keyfile = other };
    check(vf_open_slot(&r, VS_KEYFILE, &fk2, vk2, &slot) == VF_EBADKEY, "otro archivo llave → bad-key");
    VFactors fp = { .password = "otra", .keyfile = kf_hash }, fpw = { .password = "otra" };
    check(vf_open_slot(&r, VS_PASSKEY, &fp, vk2, &slot) == VF_OK && slot == 2, "contraseña + archivo llave");
    check(vf_open_slot(&r, VS_PASSKEY, &fpw, vk2, &slot) == VF_ENOSLOT, "contraseña sin la llave no alcanza");
    VFactors ff = { .fido = fido_out };
    check(vf_open_slot(&r, VS_FIDO2, &ff, vk2, &slot) == VF_OK && slot == 3, "salida FIDO2");
    char rec[64];
    snprintf(rec, sizeof rec, "%s", recovery);
    for (char *p = rec; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;     /* minúsculas */
    char nodash[64];
    int k = 0;
    for (char *p = rec; *p; p++) if (*p != '-') nodash[k++] = *p;
    nodash[k] = 0;
    VFactors fr = { .recovery = nodash };
    check(vf_open_slot(&r, VS_RECOVERY, &fr, vk2, &slot) == VF_OK && slot == 4, "clave de recuperación (minúsculas, sin guiones)");
    uint8_t *plain;
    size_t plen;
    check(vf_read_body(d, len, &r, vk2, &plain, &plen) == VF_OK && plen == strlen(PAYLOAD) && !memcmp(plain, PAYLOAD, plen), "cuerpo ida y vuelta");
    sm_free(plain);
    uint8_t pt[64];
    check(vf_att_open(vk2, r.vault_id, r.atts[0].id, d + r.atts[0].off, r.atts[0].len, r.atts[0].nonce, r.atts[0].mac, pt) == VF_OK && !memcmp(pt, "contenido del adjunto", 21), "adjunto ida y vuelta");

    /* alterar cada byte: nunca puede abrir con contenido cambiado */
    int opened_altered = 0, detected = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t *t = malloc(len);
        memcpy(t, d, len);
        t[i] ^= 0x41;
        VHeader th;
        int pr = vf_parse(t, len, &th);
        int ok = 0;
        if (pr == VF_OK) {
            uint8_t k2[32];
            if (vf_open_slot(&th, VS_PASSWORD, &good, k2, NULL) == VF_OK) {
                uint8_t *pl;
                size_t pll;
                if (vf_read_body(t, len, &th, k2, &pl, &pll) == VF_OK) {
                    ok = 1;
                    /* abrir el cuerpo pudo solo si el byte era de un adjunto: ese no tiene que abrir */
                    if (th.natts == 1 && i >= th.atts[0].off - 48 && i < th.atts[0].off + th.atts[0].len + 16) {
                        uint8_t p2[64];
                        if (vf_att_open(k2, th.vault_id, th.atts[0].id, t + th.atts[0].off, th.atts[0].len, th.atts[0].nonce, th.atts[0].mac, p2) == VF_OK
                            && !memcmp(th.atts[0].id, r.atts[0].id, 16)) opened_altered++;
                        ok = 0;
                    } else if (pll != plen || memcmp(pl, PAYLOAD, pll)) opened_altered++;
                    else {
                        /* bytes de otras ranuras (no la usada) cambian la cabecera → el cuerpo no autentica */
                        opened_altered++;
                    }
                    sm_free(pl);
                }
            }
            vf_header_free(&th);
        }
        if (!ok) detected++;
        free(t);
    }
    check(opened_altered == 0, "ningún byte alterado pasa");
    printf("alterar: %zu bytes probados, %d detectados\n", len, detected);
    /* cortado en cada largo */
    int trunc_ok = 1;
    for (size_t n = 0; n < len; n++) {
        VHeader th;
        int pr = vf_parse(d, n, &th);
        if (pr == VF_OK) { trunc_ok = 0; vf_header_free(&th); }
    }
    check(trunc_ok, "cortado en cualquier largo → error");
    VHeader th;
    check(vf_parse(d, len - 1, &th) == VF_ETRUNC || vf_parse(d, len - 1, &th) == VF_EFORMAT, "cortado al final se reconoce");
    check(vf_parse((const uint8_t *)"PK\x03\x04 no es una boveda", 24, &th) == VF_EFORMAT, "otro archivo → not-a-vault");
    {
        uint8_t *t = malloc(len);
        memcpy(t, d, len);
        t[8] = 9;
        check(vf_parse(t, len, &th) == VF_EVERSION, "versión desconocida");
        free(t);
    }
    vf_header_free(&r);
    free(d);
    unlink("/tmp/vault-test-pw-116.lvault");
}

static void test_save(void) {
    char dir[] = "/tmp/vault-test-pw-116-XXXXXX";
    check(mkdtemp(dir) != NULL, "carpeta temporal");
    int dfd = open(dir, O_RDONLY | O_DIRECTORY);
    VHeader h;
    uint8_t vk[32];
    vf_header_new(&h);
    vc_random(vk, 32);
    VFactors f = { .password = "x" };
    VSlot s;
    memset(&s, 0, sizeof s);
    vf_make_slot(&h, &s, VS_PASSWORD, &f, vk, &fast, "");
    h.slots[h.nslots++] = s;
    const char *v1 = "{\"v\":1,\"entries\":[]}", *v2 = "{\"v\":1,\"meta\":{\"x\":2},\"entries\":[]}";
    check(vf_save(dfd, "b.lvault", &h, vk, (const uint8_t *)v1, strlen(v1), NULL, 0) == VF_OK, "guardar 1");
    struct stat st;
    check(fstatat(dfd, "b.lvault.bak", &st, 0) != 0, "sin .bak la primera vez");
    check(vf_save(dfd, "b.lvault", &h, vk, (const uint8_t *)v2, strlen(v2), NULL, 0) == VF_OK, "guardar 2");
    int bf = openat(dfd, "b.lvault.bak", O_RDONLY), cf = openat(dfd, "b.lvault", O_RDONLY);
    check(bf >= 0 && cf >= 0, ".bak y actual existen");
    size_t bl, cl;
    uint8_t *bd = file_bytes(bf, &bl), *cd = file_bytes(cf, &cl);
    VHeader bh, ch;
    uint8_t *pt;
    size_t pl;
    int ok = vf_parse(bd, bl, &bh) == VF_OK && vf_read_body(bd, bl, &bh, vk, &pt, &pl) == VF_OK && pl == strlen(v1) && !memcmp(pt, v1, pl);
    check(ok, ".bak es la versión anterior");
    if (ok) sm_free(pt);
    ok = vf_parse(cd, cl, &ch) == VF_OK && vf_read_body(cd, cl, &ch, vk, &pt, &pl) == VF_OK && pl == strlen(v2) && !memcmp(pt, v2, pl);
    check(ok, "el archivo es la versión nueva");
    if (ok) sm_free(pt);
    check(ch.counter == bh.counter + 1, "contador de guardados");
    close(bf); close(cf); free(bd); free(cd);
    /* no quedan temporales */
    char cmd[200];
    snprintf(cmd, sizeof cmd, "test $(ls -A %s | wc -l) -eq 2", dir);
    check(system(cmd) == 0, "sin archivos temporales");
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) { /* */ }
    close(dfd);
}

static void test_model(void) {
    VModel m;
    check(vm_load(&m, PAYLOAD, strlen(PAYLOAD)) == 0 && m.n == 1, "cargar contenido");
    VEntry *e = m.e[0];
    check(!strcmp(vm_field(e, "password"), "s3cr3t-\"q\"") && e->fav && e->ntags == 1, "campos y escapes");
    /* buscar sin tildes ni mayúsculas */
    VjB b;
    vb_init(&b);
    vm_search_json(&b, &m, "ÑANDU correo", 10);
    check(b.p && strstr(b.p, "00112233"), "búsqueda sin tildes");
    vb_free(&b);
    vb_init(&b);
    vm_search_json(&b, &m, "s3cr3t", 10);
    check(b.p && !strcmp(b.p, "[]"), "la contraseña no se busca");
    vb_free(&b);
    /* actualizar con keep: el valor oculto queda; cambia el usuario → historial */
    const char *upd = "{\"id\":\"00112233445566778899aabbccddeeff\",\"cat\":\"internet\",\"title\":\"Correo de Ñandú\",\"tags\":[\"trabajo\"],"
                      "\"f\":[{\"k\":\"username\",\"t\":\"text\",\"v\":\"otra@example.com\"},{\"k\":\"password\",\"t\":\"hidden\",\"keep\":true}]}";
    Vj j;
    vj_parse(&j, upd, strlen(upd));
    char err[64];
    VEntry *ne = NULL;
    check(vm_upsert(&m, &j, 0, &ne, err, sizeof err) == 0 && ne, "actualizar");
    vj_free(&j);
    check(ne && !strcmp(vm_field(ne, "password"), "s3cr3t-\"q\"") && !strcmp(vm_field(ne, "username"), "otra@example.com"), "keep conserva el oculto");
    check(ne && ne->hist && strstr(ne->hist, "ana@example.com"), "historial con la versión anterior");
    vb_init(&b);
    vm_entry_json(&b, ne, 0, 1);
    check(b.p && !strstr(b.p, "s3cr3t") && strstr(b.p, "\"has\":true"), "sin reveal no salen secretos (tampoco del historial)");
    vb_free(&b);
    vb_init(&b);
    vm_entry_json(&b, ne, 1, 0);
    check(b.p && strstr(b.p, "s3cr3t"), "con reveal sí");
    vb_free(&b);
    /* ida y vuelta del contenido */
    size_t dl;
    char *dump = vm_dump(&m, &dl);
    VModel m2;
    check(dump && vm_load(&m2, dump, dl) == 0 && m2.n == 1 && !strcmp(vm_field(m2.e[0], "username"), "otra@example.com"), "contenido ida y vuelta");
    vm_free(&m2);
    sm_free(dump);
    /* 10 000 entradas: búsqueda < 10 ms */
    for (int i = 0; i < 10000; i++) {
        char js[512];
        snprintf(js, sizeof js, "{\"cat\":\"internet\",\"title\":\"Sitio número %d %s\",\"tags\":[\"t%d\"],\"f\":[{\"k\":\"username\",\"t\":\"text\",\"v\":\"usuario%d@correo%d.com\"},"
                 "{\"k\":\"password\",\"t\":\"hidden\",\"v\":\"Clave-%d-xyz\"},{\"k\":\"url\",\"t\":\"url\",\"v\":\"https://sitio%d.example.org/login\"}],\"notes\":\"nota de prueba %d\"}",
                 i, i % 7 ? "banco" : "tienda", i % 50, i, i % 100, i, i, i);
        vj_parse(&j, js, strlen(js));
        vm_upsert(&m, &j, 0, NULL, err, sizeof err);
        vj_free(&j);
    }
    check(m.n == 10001, "10 000 entradas");
    double best = 1e9, worst = 0;
    const char *qs[] = { "tienda", "usuario9999", "sitio número 42", "zzz-no-existe", "correo5 banco", NULL };
    for (int q = 0; qs[q]; q++) {
        for (int rep = 0; rep < 5; rep++) {
            vb_init(&b);
            double t0 = now_ms();
            vm_search_json(&b, &m, qs[q], 100000);
            double t = now_ms() - t0;
            vb_free(&b);
            if (t < best) best = t;
            if (t > worst) worst = t;
        }
    }
    printf("búsqueda en 10 001 entradas: %.2f–%.2f ms\n", best, worst);
    check(worst < 10, "búsqueda < 10 ms");
    vb_init(&b);
    double t0 = now_ms();
    vm_list_json(&b, &m);
    printf("lista liviana: %zu KB en %.1f ms\n", b.n / 1024, now_ms() - t0);
    vb_free(&b);
    t0 = now_ms();
    dump = vm_dump(&m, &dl);
    double td = now_ms() - t0;
    t0 = now_ms();
    check(vm_load(&m2, dump, dl) == 0 && m2.n == 10001, "cargar 10 000");
    printf("contenido de 10 001 entradas: %zu KB, volcar %.1f ms, cargar %.1f ms\n", dl / 1024, td, now_ms() - t0);
    vm_free(&m2);
    sm_free(dump);
    vm_free(&m);
}

static void test_totp(void) {
    /* RFC 6238 apéndice B (8 dígitos) */
    struct { uint64_t t; const char *s1, *s256, *s512; } v[] = {
        { 59, "94287082", "46119246", "90693936" }, { 1111111109, "07081804", "68084774", "25091201" },
        { 1111111111, "14050471", "67062674", "99943326" }, { 1234567890, "89005924", "91819424", "93441116" },
        { 2000000000, "69279037", "90698825", "38618901" }, { 20000000000ull, "65353130", "77737706", "47863826" } };
    Totp a, b, c;
    memset(&a, 0, sizeof a);
    memcpy(a.key, "12345678901234567890", 20); a.keylen = 20; a.digits = 8; a.period = 30; a.algo = TOTP_SHA1;
    b = a; memcpy(b.key, "12345678901234567890123456789012", 32); b.keylen = 32; b.algo = TOTP_SHA256;
    c = a; memcpy(c.key, "1234567890123456789012345678901234567890123456789012345678901234", 64); c.keylen = 64; c.algo = TOTP_SHA512;
    int ok = 1;
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        char o[12];
        totp_format(&a, totp_code(&a, v[i].t, NULL), o); if (strcmp(o, v[i].s1)) ok = 0;
        totp_format(&b, totp_code(&b, v[i].t, NULL), o); if (strcmp(o, v[i].s256)) ok = 0;
        totp_format(&c, totp_code(&c, v[i].t, NULL), o); if (strcmp(o, v[i].s512)) ok = 0;
    }
    check(ok, "TOTP RFC 6238 (SHA-1, SHA-256, SHA-512)");
    /* RFC 4226 apéndice D */
    static const char *hotp[] = { "755224", "287082", "359152", "969429", "338314", "254676", "287922", "162583", "399871", "520489" };
    a.digits = 6;
    ok = 1;
    for (int i = 0; i < 10; i++) { char o[12]; totp_format(&a, hotp_code(&a, (uint64_t)i), o); if (strcmp(o, hotp[i])) ok = 0; }
    check(ok, "HOTP RFC 4226");
    Totp t;
    check(totp_parse("otpauth://totp/ACME%20Co:john.doe@email.com?secret=HXDMVJECJJWSRB3HWIZR4IFUGFTMXBOZ&issuer=ACME%20Co&algorithm=SHA1&digits=6&period=30", &t) == 0
          && !strcmp(t.issuer, "ACME Co") && !strcmp(t.account, "john.doe@email.com") && t.keylen == 20 && t.digits == 6, "otpauth:// (Google Authenticator)");
    check(totp_parse("JBSW Y3DP EHPK 3PXP", &t) == 0 && t.keylen == 10 && !memcmp(t.key, "Hello!\xde\xad\xbe\xef", 10), "secreto base32 suelto");
    check(totp_parse("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=11", &t) != 0, "dígitos fuera de rango");
    check(totp_parse("otpauth://totp/x?issuer=sin-secreto", &t) != 0, "sin secreto");
    check(totp_parse("no es base32 !!", &t) != 0, "texto inválido");
    char enc[64];
    base32_encode((const uint8_t *)"Hello!\xde\xad\xbe\xef", 10, enc, sizeof enc);
    check(!strcmp(enc, "JBSWY3DPEHPK3PXP"), "base32 codificar");
}

static void test_gen(void) {
    vg_set_lists(_binary_words_txt_start, (size_t)(_binary_words_txt_end - _binary_words_txt_start),
                 _binary_common_txt_start, (size_t)(_binary_common_txt_end - _binary_common_txt_start));
    check(vg_word_count() == 7776, "7776 palabras de la EFF");
    VGenOpts o = { .length = 24, .upper = 1, .lower = 1, .digits = 1, .symbols = 1, .no_ambiguous = 1 };
    char pw[256];
    double bits;
    int ok = 1;
    for (int i = 0; i < 200; i++) {
        vg_generate(&o, pw, sizeof pw, &bits);
        if (strlen(pw) != 24 || !strpbrk(pw, "ABCDEFGHJKLMNPQRSTUVWXYZ") || !strpbrk(pw, "abcdefghijkmnpqrstuvwxyz") || !strpbrk(pw, "23456789") || strpbrk(pw, "Il1O0o|")) ok = 0;
    }
    check(ok, "contraseña: largo, clases y sin ambiguos");
    check(vg_strength(pw, &bits) >= 3, "la generada es fuerte");
    VGenOpts p = { .words = 6, .capitalize = 1, .add_digit = 1 };
    snprintf(p.sep, sizeof p.sep, "-");
    vg_generate(&p, pw, sizeof pw, &bits);
    int dashes = 0;
    for (char *q = pw; *q; q++) dashes += *q == '-';
    check(dashes == 5 && bits > 77, "frase de 6 palabras (> 77 bits)");
    printf("frase de ejemplo: %d palabras, %.0f bits\n", dashes + 1, bits);
    check(vg_strength("password", &bits) == 0 && vg_common_rank("password") > 0, "«password» es común");
    check(vg_common_rank("P4ssw0rd!") > 0, "variantes leet también");
    check(vg_strength("qwerty123", &bits) == 0, "qwerty123 débil");
    check(vg_strength("aaaaaaaaaaaa", &bits) <= 1, "repetida débil");
    check(vg_strength("Tr0ub4dour&3-kangaroo-Zebra", &bits) >= 3, "larga y variada fuerte");
}

static void test_json(void) {
    Vj j;
    const char *s = "{\"a\":[1,2,{\"b\":\"x\\u00e9\\ud83d\\ude00\\n\"}],\"c\":true,\"d\":null,\"e\":-1.5e2}";
    check(vj_parse(&j, s, strlen(s)) == 0, "JSON válido");
    int a = vj_get(&j, 0, "a");
    check(vj_type(&j, a) == VJ_ARR && j.t[a].count == 3, "arreglo");
    char *b = vj_gets(&j, vj_at(&j, a, 2), "b");
    check(b && !strcmp(b, "x\xc3\xa9\xf0\x9f\x98\x80\n"), "escapes y pares sustitutos");
    sm_free(b);
    check(vj_getb(&j, 0, "c", 0) == 1 && vj_getn(&j, 0, "e", 0) == -150, "bool y número");
    vj_free(&j);
    const char *bad[] = { "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "\"\\x\"", "01", "[1] 2", "{\"a\":\"\x01\"}", "nul", "[\"\\ud800\"", NULL };
    int ok = 1;
    for (int i = 0; bad[i]; i++) if (vj_parse(&j, bad[i], strlen(bad[i])) == 0) { ok = 0; printf("aceptó: %s\n", bad[i]); vj_free(&j); }
    check(ok, "JSON inválido se rechaza");
    char deep[200];
    memset(deep, '[', 100);
    memset(deep + 100, ']', 100);
    check(vj_parse(&j, deep, 200) != 0, "profundidad máxima");
    VjB w;
    vb_init(&w);
    vb_open(&w, '{'); vb_ks(&w, "t", "a\"b\\c\x01\xff"); vb_ki(&w, "n", 3); vb_close(&w, '}');
    check(w.p && !strcmp(w.p, "{\"t\":\"a\\\"b\\\\c\\u0001\xef\xbf\xbd\",\"n\":3}"), "escritor con escapes y UTF-8 inválido");
    vb_free(&w);
    size_t n;
    uint8_t *dec = b64_decode("aGVsbG8gd29ybGQ=", 16, &n);
    check(dec && n == 11 && !memcmp(dec, "hello world", 11), "base64");
    sm_free(dec);
}

static void test_wire(void) {
    int sv[2];
    socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv);
    size_t big = 300000;
    char *msg = malloc(big + 1);
    for (size_t i = 0; i < big; i++) msg[i] = (char)('a' + i % 26);
    msg[big] = 0;
    int p[2];
    if (pipe(p)) return;
    if (fork() == 0) { vw_send(sv[0], VW_REQ, 77, msg, big, &p[1], 1); _exit(0); }
    VMsg m;
    check(vw_recv(sv[1], &m, &vw_malloc) == 0 && m.id == 77 && m.len == big && !memcmp(m.json, msg, big) && m.nfds == 1, "mensaje de 300 KB en pedazos + fd");
    if (m.nfds == 1) { check(write(m.fds[0], "x", 1) == 1, "el fd recibido sirve"); }
    vw_msg_free(&m, &vw_malloc);
    free(msg);
    close(sv[0]); close(sv[1]); close(p[0]); close(p[1]);
}

static void test_timing(void) {
    VKdf k;
    int est;
    double t0 = now_ms();
    vf_calibrate(700, &k, &est);
    double tc = now_ms() - t0;
    uint8_t salt[16] = { 1 }, out[32];
    t0 = now_ms();
    vf_argon2id((const uint8_t *)"contraseña", 11, salt, &k, out);
    double t = now_ms() - t0;
    printf("Argon2id calibrado: m=%u MiB t=%u (calibrar %.0f ms) → desbloquear %.0f ms (estimado %d)\n", k.m_kib / 1024, k.t_cost, tc, t, est);
    check(t > 250 && t < 2000, "desbloqueo en el rango (0,25–2 s)");
}

int main(void) {
    sm_harden_process();
    test_json();
    test_format();
    test_save();
    test_model();
    test_totp();
    test_gen();
    test_wire();
    test_timing();
    printf("vault-test: %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}
