/* import_test.c — importadores de la bóveda (src/vault/vimport.c, csvp.c, kdbx.c, xmlmini.c).
 *
 * Datos en tests/import/:
 *   - CSV/JSON escritos a mano con los encabezados de cada exportador (Chrome, Firefox, Apple,
 *     Bitwarden CSV y JSON) y keepassxc.csv, exportado de verdad por keepassxc-cli 2.7.10;
 *   - bóvedas KDBX reales (gen-kdbx.sh/gen-kdbx.py en la VM 116): kx3-aeskdf.kdbx (KeePassXC
 *     2.7.10, KDBX 3.1, AES-256 + AES-KDF, flujo interno Salsa20) y kx4-*.kdbx (pykeepass 4.2.0,
 *     KDBX 4.0: AES/Argon2d, ChaCha20/Argon2id + archivo llave binario, AES/Argon2id + archivo
 *     llave XML 2.0, ChaCha20/AES-KDF). La contraseña de todas es "lordos-prueba".
 * Además: ida y vuelta del CSV propio, KDBX con contraseña equivocada, alterado y cortado, y
 * miles de mutaciones (cortes y bytes cambiados) de cada muestra, con ASan/UBSan si el
 * compilador los tiene. Mide el CSV de 10 000 entradas y el desbloqueo de cada KDBX. */
#include "vault/vimport.h"
#include "vault/kdbx.h"
#include "vault/xmlmini.h"
#include "vault/csvp.h"
#include "vault/smem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DIR "tests/import/"
#define PW "lordos-prueba"

static int fails, total;
static void check(int ok, const char *what) { total++; if (!ok) { fails++; printf("FALLA: %s\n", what); } }
static int eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static uint8_t *slurp(const char *name, size_t *n) {
    char p[256];
    snprintf(p, sizeof p, DIR "%s", name);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("no está %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz + 1);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2);
    fclose(f);
    b[sz] = 0;
    *n = (size_t)sz;
    return b;
}

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static const ImpEntry *find(const ImpList *l, const char *title) {
    for (int i = 0; i < l->n; i++) if (eq(l->v[i].title, title)) return &l->v[i];
    return NULL;
}
static const char *field(const ImpEntry *e, const char *key, int *type) {
    for (int i = 0; e && i < e->nfields; i++) if (eq(e->fields[i].key, key)) { if (type) *type = e->fields[i].type; return e->fields[i].value; }
    return NULL;
}
static int has_tag(const ImpEntry *e, const char *t) { for (int i = 0; e && i < e->ntags; i++) if (eq(e->tags[i], t)) return 1; return 0; }

static int parse_file(const char *name, int kind, const char *pw, const char *kf, ImpList *l) {
    size_t n, kn = 0;
    uint8_t *d = slurp(name, &n), *k = kf ? slurp(kf, &kn) : NULL;
    int r = imp_parse(kind, d, n, pw, k, kn, l);
    free(d);
    free(k);
    return r;
}

/* ---------- CSV y JSON ---------- */

static void test_csv_reader(void) {
    const char *s = "\xef\xbb\xbf" "a,\"b,1\",\"c\"\"d\"\r\n\r\n\"multi\nlínea\",,x\nlast,";
    Csv c;
    CsvRow r = { 0 };
    csv_init(&c, s, strlen(s));
    check(csv_next(&c, &r) == 1 && r.n == 3 && eq(r.f[0], "a") && eq(r.f[1], "b,1") && eq(r.f[2], "c\"d"), "CSV: comillas y BOM");
    check(csv_next(&c, &r) == 1 && r.n == 3 && eq(r.f[0], "multi\nlínea") && eq(r.f[1], "") && eq(r.f[2], "x"), "CSV: salto adentro y vacío");
    check(csv_next(&c, &r) == 1 && r.n == 2 && eq(r.f[1], ""), "CSV: coma final");
    check(csv_next(&c, &r) == 0, "CSV: fin");
    csv_row_free(&r);
    const char *bad = "a,\"sin cerrar\n";
    csv_init(&c, bad, strlen(bad));
    check(csv_next(&c, &r) == -1, "CSV: comillas sin cerrar");
    csv_row_free(&r);
}

static void test_formats(void) {
    ImpList l;
    size_t n;
    uint8_t *d;
    struct { const char *f; int kind; } det[] = {
        { "chrome.csv", IMP_CHROME_CSV }, { "firefox.csv", IMP_FIREFOX_CSV }, { "apple.csv", IMP_APPLE_CSV },
        { "bitwarden.csv", IMP_BITWARDEN_CSV }, { "bitwarden.json", IMP_BITWARDEN_JSON }, { "keepassxc.csv", IMP_KEEPASSXC_CSV },
        { "kx4-aes-argon2d.kdbx", IMP_KDBX }, { "kx3-aeskdf.kdbx", IMP_KDBX }, { "kdbx-sample.xml", IMP_UNKNOWN },
    };
    for (size_t i = 0; i < sizeof det / sizeof *det; i++) {
        d = slurp(det[i].f, &n);
        char w[80];
        snprintf(w, sizeof w, "detectar %s", det[i].f);
        check(imp_detect(d, n, det[i].f) == det[i].kind, w);
        free(d);
    }

    check(parse_file("chrome.csv", 0, NULL, NULL, &l) == 0 && l.n == 3, "Chrome: 3 entradas");
    const ImpEntry *e = find(&l, "ejemplo.com");
    check(e && eq(e->password, "pa,ss\"word") && eq(e->username, "ana@ejemplo.com") && eq(e->url, "https://ejemplo.com/login") && !e->notes, "Chrome: campos");
    e = find(&l, "banco.example");
    check(e && eq(e->password, "ñandú-€-123") && eq(e->notes, "nota con\nsalto de línea"), "Chrome: unicode y nota");
    check(find(&l, "sin-nombre.example.org") != NULL, "Chrome: título desde la URL");
    imp_free(&l);

    check(parse_file("firefox.csv", 0, NULL, NULL, &l) == 0 && l.n == 2, "Firefox: 2 entradas");
    e = find(&l, "mozilla.org");
    check(e && eq(e->password, "r3d\"panda") && e->created == 1700000000 && e->modified == 1700000600, "Firefox: campos y fechas");
    e = find(&l, "accounts.example.com");
    check(e && !e->username && eq(e->password, "only-pass"), "Firefox: sin usuario");
    imp_free(&l);

    check(parse_file("apple.csv", 0, NULL, NULL, &l) == 0 && l.n == 2, "Apple: 2 entradas");
    e = find(&l, "example.com (ana)");
    check(e && eq(e->password, "x,y") && e->otp && !strncmp(e->otp, "otpauth://totp/example.com:ana?secret=JBSWY3DPEHPK3PXP", 53), "Apple: OTPAuth");
    e = find(&l, "GitHub");
    check(e && eq(e->notes, "Nota \"importante\"") && !e->otp, "Apple: nota");
    imp_free(&l);

    check(parse_file("bitwarden.csv", 0, NULL, NULL, &l) == 0 && l.n == 2, "Bitwarden CSV: 2 entradas");
    e = find(&l, "Twitter");
    int ft = -1;
    check(e && e->favorite && eq(e->group, "Redes Sociales") && eq(e->url, "https://twitter.com") && e->nurls == 1 && eq(e->urls[0], "https://x.com") &&
          eq(field(e, "PIN", &ft), "1234") && eq(field(e, "campo1", NULL), "valor1") && eq(e->otp, "JBSWY3DPEHPK3PXP"), "Bitwarden CSV: login");
    e = find(&l, "Nota secreta");
    check(e && eq(e->kind, "note") && eq(e->notes, "Contenido de la nota") && !e->group, "Bitwarden CSV: nota");
    imp_free(&l);

    check(parse_file("bitwarden.json", 0, NULL, NULL, &l) == 0 && l.n == 4, "Bitwarden JSON: 4 entradas");
    check(l.ngroups == 2, "Bitwarden JSON: carpetas (también la vacía)");
    e = find(&l, "Banco Ñandú");
    check(e && e->favorite && eq(e->group, "Finanzas") && eq(e->username, "juan") && eq(e->password, "p@\"ss\\w0rd") &&
          eq(e->url, "https://banco.example.com") && e->nurls == 1 && eq(e->notes, "línea1\nlínea2") && e->otp &&
          e->created == 1704164645 && e->modified == 1738555506, "Bitwarden JSON: login");
    ft = -1;
    check(eq(field(e, "PIN", &ft), "9876") && ft == IMPF_HIDDEN && eq(field(e, "Cliente", NULL), "000123") &&
          eq(field(e, "Activo", NULL), "true") && !field(e, "Vinculado", NULL), "Bitwarden JSON: campos propios");
    e = find(&l, "Visa");
    ft = -1;
    check(e && eq(e->kind, "card") && eq(field(e, "holder", NULL), "Juan Pérez") && eq(field(e, "number", &ft), "4111111111111111") && ft == IMPF_HIDDEN &&
          eq(field(e, "expiry", NULL), "07/2029") && eq(field(e, "cvv", NULL), "123") && eq(e->group, "Finanzas"), "Bitwarden JSON: tarjeta");
    e = find(&l, "Yo");
    check(e && eq(e->kind, "identity") && eq(field(e, "lastName", NULL), "Pérez") && eq(field(e, "passportNumber", NULL), "AAA123") && !field(e, "middleName", NULL), "Bitwarden JSON: identidad");
    e = find(&l, "Nota");
    check(e && eq(e->kind, "note") && eq(e->notes, "texto secreto \xf0\x9f\x94\x91"), "Bitwarden JSON: nota con emoji (pares sustitutos)");
    imp_free(&l);
    check(parse_file("bitwarden-encrypted.json", 0, NULL, NULL, &l) == -1 && eq(l.err, "encrypted-export"), "Bitwarden JSON cifrado: aviso");

    check(parse_file("keepassxc.csv", 0, NULL, NULL, &l) == 0 && l.n == 4, "KeePassXC CSV: 4 entradas (sin la papelera)");
    e = find(&l, "Banco Ñandú");
    check(e && eq(e->group, "Internet/Bancos") && eq(e->password, "s3cr3t&\"<€") && eq(e->notes, "línea 1\nlínea 2") && e->otp &&
          e->created == 1709634030 && e->modified == 1735787045, "KeePassXC CSV: campos, carpeta y fechas");
    e = find(&l, "Raíz suelta");
    check(e && !e->group, "KeePassXC CSV: entrada de la raíz");
    imp_free(&l);
}

static void test_roundtrip(void) {
    ImpList a, b;
    check(parse_file("bitwarden.json", 0, NULL, NULL, &a) == 0, "ida y vuelta: leer");
    /* agregar etiquetas para probarlas también */
    ImpEntry *e = &a.v[0];
    e->tags = sm_alloc(2 * sizeof(char *));
    e->tags[0] = sm_strdup("uno");
    e->tags[1] = sm_strdup("dos tres");
    e->ntags = 2;
    char *csv = NULL;
    size_t n = 0;
    check(exp_csv(&a, &csv, &n) == 0, "exportar CSV propio");
    check(imp_detect((uint8_t *)csv, n, NULL) == IMP_LORD_CSV, "detectar CSV propio");
    check(imp_parse(0, (uint8_t *)csv, n, NULL, NULL, 0, &b) == 0 && b.n == a.n, "volver a leer el CSV propio");
    int same = 1;
    for (int i = 0; i < a.n && i < b.n; i++) {
        const ImpEntry *x = &a.v[i], *y = &b.v[i];
#define S(f) if (!((!x->f && !y->f) || eq(x->f, y->f))) { same = 0; printf("  distinto %s en %d: «%s» «%s»\n", #f, i, x->f ? x->f : "-", y->f ? y->f : "-"); }
        S(title) S(username) S(password) S(url) S(notes) S(otp) S(group) S(kind)
#undef S
        if (x->ntags != y->ntags || x->nfields != y->nfields || x->nurls != y->nurls || x->favorite != y->favorite || x->created != y->created || x->modified != y->modified) same = 0;
        for (int k = 0; same && k < x->nfields; k++)
            if (!eq(x->fields[k].key, y->fields[k].key) || !eq(x->fields[k].value, y->fields[k].value) || x->fields[k].type != y->fields[k].type) same = 0;
        for (int k = 0; same && k < x->ntags; k++) if (!eq(x->tags[k], y->tags[k])) same = 0;
    }
    check(same, "CSV propio: todo igual");
    sm_free(csv);
    imp_free(&a);
    imp_free(&b);
}

/* ---------- KDBX ---------- */

static void check_kdbx_content(const ImpList *l, const char *name, int v4) {
    char w[160];
#define C(cond, what) do { snprintf(w, sizeof w, "%s: %s", name, what); check(cond, w); } while (0)
    C(l->n == 4, "4 entradas (sin la papelera)");
    const ImpEntry *e = find(l, "Banco Ñandú");
    C(e != NULL, "entrada del banco");
    if (!e) return;
    C(eq(e->username, "juan.pérez") && eq(e->password, "s3cr3t&\"<€") && eq(e->url, "https://banco.example.com/login"), "usuario, contraseña (protegida) y URL");
    C(eq(e->notes, "línea 1\nlínea 2"), "notas");
    C(e->otp && strstr(e->otp, "secret=JBSWY3DPEHPK3PXP"), "TOTP");
    C(eq(e->group, "Internet/Bancos"), "carpeta anidada");
    C(has_tag(e, "finanzas") && has_tag(e, "importante") && e->ntags == 2, "etiquetas");
    int ft = -1;
    C(eq(field(e, "PIN", &ft), "4321") && ft == IMPF_HIDDEN, "campo protegido");
    ft = -1;
    C(eq(field(e, "Cliente", &ft), "00012345") && ft == IMPF_TEXT, "campo de texto");
    C(e->natts == 1 && eq(e->atts[0].name, "adjunto.txt") && e->atts[0].len == 25 && !memcmp(e->atts[0].data, "Hola, adjunto de prueba!\n", 25), "adjunto");
    C(e->created == 1709634030 && e->modified == 1735787045, "fechas");
    const ImpEntry *s = find(l, "vps1");
    C(s && eq(s->group, "Servidores") && eq(s->password, "ssh-pass"), "otra carpeta");
    const ImpEntry *r = find(l, "Raíz suelta");
    C(r && !r->group && eq(r->password, "p"), "entrada de la raíz");
    C(!find(l, "Borrada"), "la papelera no se importa");
    (void)v4;
#undef C
}

static void test_kdbx(void) {
    struct { const char *f, *kf; int v4; } dbs[] = {
        { "kx4-aes-argon2d.kdbx", NULL, 1 },
        { "kx4-chacha-argon2id-key.kdbx", "kx-keyfile.key", 1 },
        { "kx4-aes-argon2id-keyx.kdbx", "kx-keyfile2.keyx", 1 },
        { "kx4-chacha-aeskdf.kdbx", NULL, 1 },
        { "kx3-aeskdf.kdbx", NULL, 0 },
    };
    for (size_t i = 0; i < sizeof dbs / sizeof *dbs; i++) {
        ImpList l;
        double t0 = now_ms();
        int r = parse_file(dbs[i].f, 0, PW, dbs[i].kf, &l);
        double t1 = now_ms();
        char w[320];
        snprintf(w, sizeof w, "%s: abre (%s)", dbs[i].f, r ? l.err : "ok");
        check(r == 0, w);
        printf("  %s: %.1f ms\n", dbs[i].f, t1 - t0);
        if (!r) check_kdbx_content(&l, dbs[i].f, dbs[i].v4);
        imp_free(&l);

        /* contraseña equivocada / sin el archivo llave */
        r = parse_file(dbs[i].f, 0, "otra", dbs[i].kf, &l);
        snprintf(w, sizeof w, "%s: contraseña equivocada → bad-password (%s)", dbs[i].f, l.err);
        check(r == -1 && eq(l.err, "bad-password"), w);
        if (dbs[i].kf) {
            r = parse_file(dbs[i].f, 0, PW, NULL, &l);
            snprintf(w, sizeof w, "%s: sin archivo llave → bad-password (%s)", dbs[i].f, l.err);
            check(r == -1 && eq(l.err, "bad-password"), w);
        }

        /* alterado: un byte de la cabecera y uno del cuerpo; cortado */
        size_t n;
        uint8_t *d = slurp(dbs[i].f, &n);
        size_t kn = 0;
        uint8_t *k = dbs[i].kf ? slurp(dbs[i].kf, &kn) : NULL;
        d[40] ^= 1;
        r = imp_parse(0, d, n, PW, k, kn, &l);
        snprintf(w, sizeof w, "%s: cabecera alterada → error (%s)", dbs[i].f, l.err);
        check(r == -1 && (dbs[i].v4 ? eq(l.err, "corrupt") : (eq(l.err, "corrupt") || eq(l.err, "bad-password"))), w);
        d[40] ^= 1;
        d[n - 40] ^= 0x10;
        r = imp_parse(0, d, n, PW, k, kn, &l);
        snprintf(w, sizeof w, "%s: cuerpo alterado → corrupt (%s)", dbs[i].f, l.err);
        check(r == -1 && (eq(l.err, "corrupt") || (!dbs[i].v4 && eq(l.err, "bad-password"))), w);
        d[n - 40] ^= 0x10;
        size_t cuts[] = { 0, 5, 11, 60, 200, n / 2, n - 17, n - 1 };
        int ok = 1;
        for (size_t c = 0; c < sizeof cuts / sizeof *cuts; c++) {
            r = imp_parse(0, d, cuts[c], PW, k, kn, &l);
            if (r != -1 || !(eq(l.err, "truncated") || eq(l.err, "corrupt") || eq(l.err, "empty") || eq(l.err, "unknown-format") || eq(l.err, "bad-password"))) {
                ok = 0;
                printf("  corte en %zu: %d %s\n", cuts[c], r, l.err);
            }
        }
        snprintf(w, sizeof w, "%s: cortado → truncated/corrupt", dbs[i].f);
        check(ok, w);
        free(d);
        free(k);
    }

    /* AES-KDF: AES-NI y tablas T contra el AES de referencia (vcrypto.c) */
    {
        uint8_t c[32], sd[32], r1[32], r2[32], r3[32];
        for (int i = 0; i < 32; i++) { c[i] = (uint8_t)(i * 7 + 1); sd[i] = (uint8_t)(255 - i * 3); }
        kdbx_aes_kdf_ref(c, sd, 1000, r1);
        kdbx_aes_kdf_mode = 0; kdbx_aes_kdf(c, sd, 1000, r2);
        kdbx_aes_kdf_mode = 1; kdbx_aes_kdf(c, sd, 1000, r3);
        kdbx_aes_kdf_mode = 0;
        check(!memcmp(r1, r2, 32) && !memcmp(r1, r3, 32), "AES-KDF rápido = referencia");
    }

    /* archivos llave */
    uint8_t out[32], want[32];
    const char *hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    for (int i = 0; i < 32; i++) want[i] = (uint8_t)i;
    check(kdbx_keyfile_key((const uint8_t *)hex, 64, out) == 0 && !memcmp(out, want, 32), "archivo llave de 64 hex");
    check(kdbx_keyfile_key(want, 32, out) == 0 && !memcmp(out, want, 32), "archivo llave de 32 bytes");
    const char *x1 = "<?xml version=\"1.0\"?><KeyFile><Meta><Version>1.00</Version></Meta><Key><Data>AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=</Data></Key></KeyFile>";
    check(kdbx_keyfile_key((const uint8_t *)x1, strlen(x1), out) == 0 && !memcmp(out, want, 32), "archivo llave XML 1.0");
    const char *x2 = "<KeyFile><Meta><Version>2.0</Version></Meta><Key><Data Hash=\"DEADBEEF\">00010203 04050607 08090A0B 0C0D0E0F 10111213 14151617 18191A1B 1C1D1E1F</Data></Key></KeyFile>";
    check(kdbx_keyfile_key((const uint8_t *)x2, strlen(x2), out) == -1, "archivo llave XML 2.0 con Hash equivocado");
}

/* ---------- XML ---------- */

static void test_xml(void) {
    const char *ok = "<?xml version=\"1.0\"?><!-- c --><a x='1' y=\"&lt;&amp;&#65;&#x42;\"><b>t&gt;<![CDATA[<raw>&amp;]]></b><c/></a>";
    XNode *r = xml_parse(ok, strlen(ok), NULL);
    check(r && eq(r->name, "a") && eq(xml_attr(r, "y"), "<&AB") && eq(xml_text(xml_child(r, "b")), "t><raw>&amp;") && xml_child(r, "c"), "XML: entidades, CDATA, vacío");
    xml_free(r);
    const char *bad[] = {
        "<!DOCTYPE a [<!ENTITY x \"y\">]><a>&x;</a>", "<a><b></a></b>", "<a>&unknown;</a>", "<a", "<a></a><b></b>", "<a x=1></a>", "texto<a/>",
    };
    int all = 1;
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) { r = xml_parse(bad[i], strlen(bad[i]), NULL); if (r) { all = 0; xml_free(r); printf("  aceptó: %s\n", bad[i]); } }
    check(all, "XML: rechaza DOCTYPE, mal anidado, entidades desconocidas, cortado");
    /* profundidad */
    char *deep = malloc(6000);
    size_t n = 0;
    for (int i = 0; i < 1000; i++) { memcpy(deep + n, "<a>", 3); n += 3; }
    r = xml_parse(deep, n, NULL);
    check(!r, "XML: tope de profundidad");
    xml_free(r);
    free(deep);
}

/* ---------- mutaciones ---------- */

static uint64_t rng = 0x5eed1234abcdULL;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }

static void fuzz(void) {
    const char *files[] = { "chrome.csv", "firefox.csv", "apple.csv", "bitwarden.csv", "bitwarden.json", "keepassxc.csv",
                            "kdbx-sample.xml", "kx4-aes-argon2d.kdbx", "kx4-chacha-aeskdf.kdbx", "kx3-aeskdf.kdbx", "kx-keyfile2.keyx" };
    long runs = 0, okparse = 0;
    double t0 = now_ms();
    for (size_t f = 0; f < sizeof files / sizeof *files; f++) {
        size_t n;
        uint8_t *orig = slurp(files[f], &n);
        int kdbx = strstr(files[f], ".kdbx") != NULL;
        int xml = strstr(files[f], ".xml") || strstr(files[f], ".keyx");
        uint8_t *d = malloc(n + 1);
        int iters = kdbx ? 60 : 500;
        for (int it = 0; it < iters; it++) {
            memcpy(d, orig, n);
            size_t len = n;
            int mode = (int)(rnd() % 3);
            if (mode == 0) len = rnd() % (n + 1);                       /* corte */
            else {
                int flips = 1 + (int)(rnd() % 4);
                for (int k = 0; k < flips; k++) {
                    size_t pos = rnd() % n;
                    if (kdbx && !strncmp(files[f], "kx3", 3) && pos < 240) pos += 240;   /* v3: la cabecera no tiene hash: no inflar las rondas */
                    if (pos >= n) pos = n - 1;
                    d[pos] = mode == 1 ? (uint8_t)(d[pos] ^ (1u << (rnd() % 8))) : (uint8_t)rnd();
                }
                if (mode == 2 && rnd() % 2) len = rnd() % (n + 1);
            }
            if (xml) {
                XNode *r = xml_parse((const char *)d, len, NULL);
                xml_free(r);
                uint8_t k[32];
                kdbx_keyfile_key(d, len, k);
            }
            ImpList l;
            for (int kind = 0; kind <= (kdbx ? 0 : IMP_LORD_CSV); kind += (kdbx ? 1 : 1)) {
                if (kind == IMP_KDBX && !kdbx) continue;
                int r = imp_parse(kind, d, len, PW, NULL, 0, &l);
                runs++;
                if (!r) { okparse++; char *csv; size_t cn; if (!exp_csv(&l, &csv, &cn)) sm_free(csv); imp_free(&l); }
            }
        }
        free(d);
        free(orig);
    }
    printf("  mutaciones: %ld lecturas (%ld aceptadas) en %.0f ms\n", runs, okparse, now_ms() - t0);
    check(1, "mutaciones sin caerse");
}

/* ---------- rendimiento ---------- */

static void bench(void) {
    size_t cap = 10000 * 120 + 64, n = 0;
    char *s = malloc(cap);
    n += (size_t)snprintf(s + n, cap - n, "name,url,username,password,note\n");
    for (int i = 0; i < 10000; i++)
        n += (size_t)snprintf(s + n, cap - n, "sitio%d.example.com,https://sitio%d.example.com/login,usuario%d@example.com,\"cl,ave-%08x\",\n", i, i, i, (unsigned)(i * 2654435761u));
    ImpList l;
    double t0 = now_ms();
    int r = imp_parse(0, (uint8_t *)s, n, NULL, NULL, 0, &l);
    double t1 = now_ms();
    check(r == 0 && l.n == 10000, "CSV de 10 000 entradas");
    printf("  CSV de Chrome con 10 000 entradas (%zu KB): %.1f ms\n", n / 1024, t1 - t0);
    char *csv;
    size_t cn;
    t0 = now_ms();
    r = exp_csv(&l, &csv, &cn);
    printf("  exportar 10 000 entradas: %.1f ms\n", now_ms() - t0);
    if (!r) sm_free(csv);
    imp_free(&l);
    free(s);
}

int main(void) {
    /* topes chicos: las mutaciones no pueden pedir KDF enormes */
    kdbx_max_aes_rounds = 200000;
    kdbx_max_argon_kib = 8192;
    test_csv_reader();
    test_formats();
    test_roundtrip();
    test_xml();
    test_kdbx();
    fuzz();
    bench();
    /* un KDBX real con los valores de KeePassXC (si se pasó KDBX_BENCH=archivo) */
    const char *b = getenv("KDBX_BENCH");
    if (b) {
        kdbx_max_aes_rounds = 500000000ull;
        kdbx_max_argon_kib = 2ull * 1024 * 1024;
        if (getenv("KDBX_TTABLE")) kdbx_aes_kdf_mode = 1;
        FILE *f = fopen(b, "rb");
        if (f) {
            static uint8_t buf[1 << 22];
            size_t n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            ImpList l;
            double t0 = now_ms();
            int r = imp_parse(0, buf, n, getenv("KDBX_PW") ? getenv("KDBX_PW") : PW, NULL, 0, &l);
            printf("  %s: %s, %d entradas, %.0f ms\n", b, r ? l.err : "ok", l.n, now_ms() - t0);
            imp_free(&l);
        }
    }
    printf("import-test: %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}
