/* sign.c — verificar paquetes .lpk firmados y el catálogo firmado (ver sign.h y docs/FIRMA.md).
 *
 * SHA-256 y Ed25519 son los propios de server/src/pkg/crypto.c (Ed25519 con la aritmética de
 * TweetNaCl, dominio público; probados con los vectores de RFC 8032 y NIST en tests/sign_test.c).
 * Nada de OpenSSL: por política solo lo enlaza lord-sessiond. */
#include "sign.h"
#include "json.h"
#include "keys.h"
#include "../pkg/crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CAT_MAX (4u * 1024 * 1024)

static void seterr(char *e, size_t n, const char *m) { if (e && n) snprintf(e, n, "%s", m); }

const char *sig_trust_name(LpkTrust t) {
    return t == LPK_OFFICIAL ? "lordos" : t == LPK_VERIFIED ? "verified" : t == LPK_SIGNED ? "signed" : "unsigned";
}

static int is_hex(const char *s, size_t n) {
    if (!s || strlen(s) != n) return 0;
    for (size_t i = 0; i < n; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

#ifdef LORD_SIGN_TEST
/* solo en tests/sign_test.c: una clave de prueba hace de oficial (para el catálogo) */
static char test_official[65];
void sig_test_official(const char *keyhex) { snprintf(test_official, sizeof test_official, "%s", keyhex); }
#endif

int sig_official(const char *keyhex) {
    for (int i = 0; lordos_official_keys[i]; i++) if (!strcmp(lordos_official_keys[i], keyhex)) return 1;
#ifdef LORD_SIGN_TEST
    if (test_official[0] && !strcmp(test_official, keyhex)) return 1;
#endif
    return 0;
}

int sig_verify_detached(const char *sig_json, size_t siglen, const void *data, size_t len, char key[65], char *err, size_t errlen) {
    JDoc d;
    char why[96];
    if (json_parse(sig_json, siglen, &d, why, sizeof why)) { seterr(err, errlen, "firma.json inválido"); return -1; }
    const char *alg = json_str(d.root, "alg"), *k = json_str(d.root, "key"), *s = json_str(d.root, "sig");
    int r = -1;
    uint8_t pub[32], sig[64];
    if (json_num(d.root, "format", 0) != 1 || !alg || strcmp(alg, "ed25519")) seterr(err, errlen, "firma de un formato desconocido");
    else if (!is_hex(k, 64) || !is_hex(s, 128) || hex_decode(k, pub, 32) || hex_decode(s, sig, 64)) seterr(err, errlen, "firma.json inválido");
    else if (!ed25519_verify(pub, sig, data, len)) seterr(err, errlen, "la firma no corresponde al contenido");
    else { snprintf(key, 65, "%s", k); r = 0; }
    json_free(&d);
    return r;
}

int sig_make_detached(const uint8_t seed[32], const void *data, size_t len, char *out, size_t outlen) {
    uint8_t pub[32], sig[64];
    char kh[65], sh[129];
    ed25519_public(seed, pub);
    ed25519_sign(seed, data, len, sig);
    hex_encode(pub, 32, kh);
    hex_encode(sig, 64, sh);
    int n = snprintf(out, outlen, "{\n  \"format\": 1,\n  \"alg\": \"ed25519\",\n  \"key\": \"%s\",\n  \"sig\": \"%s\"\n}\n", kh, sh);
    return n > 0 && (size_t)n < outlen ? 0 : -1;
}

/* contenido.json contra los archivos del paquete: cada archivo (salvo contenido.json y firma.json)
 * aparece una vez, con el mismo tamaño y SHA-256, y no sobra ni falta ninguno. */
static int check_contents(const Lpk *pk, const JVal *root, const char *id, const char *version, char *err, size_t errlen) {
    const char *cid = json_str(root, "id"), *cver = json_str(root, "version");
    const JVal *files = json_get(root, "files");
    if (json_num(root, "format", 0) != 1) { seterr(err, errlen, "contenido.json de un formato desconocido"); return -1; }
    if (!cid || strcmp(cid, id) || !cver || strcmp(cver, version)) { seterr(err, errlen, "contenido.json es de otra app o de otra versión"); return -1; }
    if (!files || files->type != J_ARR) { seterr(err, errlen, "contenido.json no tiene la lista de archivos"); return -1; }
    int listed = 0;
    for (const JVal *f = files->kid; f; f = f->next) {
        const char *path = json_str(f, "path"), *sha = json_str(f, "sha256");
        double size = json_num(f, "size", -1);
        if (!path || !lpk_valid_path(path) || !strcmp(path, SIG_CONTENTS) || !strcmp(path, SIG_SIGNATURE) || !is_hex(sha, 64) || size < 0) {
            seterr(err, errlen, "contenido.json tiene una entrada inválida");
            return -1;
        }
        for (const JVal *g = files->kid; g != f; g = g->next)
            if (!strcmp(json_str(g, "path"), path)) { seterr(err, errlen, "contenido.json repite un archivo"); return -1; }
        const LpkFile *lf = lpk_find(pk, path);
        if (!lf) { if (err && errlen) snprintf(err, errlen, "falta un archivo del paquete: %s", path); return -1; }
        uint8_t h[32];
        char hx[65];
        sha256(lf->data, lf->size, h);
        hex_encode(h, 32, hx);
        if ((double)lf->size != size || strcmp(hx, sha)) { if (err && errlen) snprintf(err, errlen, "el archivo %s fue modificado", path); return -1; }
        listed++;
    }
    int real = 0;
    for (int i = 0; i < pk->nfiles; i++)
        if (strcmp(pk->files[i].path, SIG_CONTENTS) && strcmp(pk->files[i].path, SIG_SIGNATURE)) real++;
    if (real != listed) { seterr(err, errlen, "el paquete trae archivos que no están en contenido.json"); return -1; }
    return 0;
}

int sig_verify_package(const Lpk *pk, const char *id, const char *version, LpkSigInfo *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    const LpkFile *c = lpk_find(pk, SIG_CONTENTS), *s = lpk_find(pk, SIG_SIGNATURE);
    out->has_contents = c != NULL;
    out->has_signature = s != NULL;
    if (!c) {
        if (s) { seterr(err, errlen, "el paquete tiene firma pero no contenido.json"); return -1; }
        out->trust = LPK_UNSIGNED;
        if (catalog_revoked("", id, version, "", out->revoked_why, sizeof out->revoked_why)) out->revoked = 1;
        return 0;
    }
    uint8_t h[32];
    sha256(c->data, c->size, h);
    hex_encode(h, 32, out->contents_sha);
    JDoc d;
    char why[96];
    if (json_parse((const char *)c->data, c->size, &d, why, sizeof why)) {
        if (err && errlen) snprintf(err, errlen, "contenido.json inválido: %s", why);
        return -1;
    }
    int r = check_contents(pk, d.root, id, version, err, errlen);
    const JVal *signer = json_get(d.root, "signer");
    const char *sname = json_str(signer, "name"), *skey = json_str(signer, "key");
    if (!r && s) {
        r = sig_verify_detached((const char *)s->data, s->size, c->data, c->size, out->key, err, errlen);
        if (!r && (!skey || strcmp(skey, out->key))) { seterr(err, errlen, "contenido.json nombra otra clave que la de la firma"); r = -1; }
    }
    if (!r) {
        if (!s) out->trust = LPK_UNSIGNED;
        else if (sig_official(out->key)) { out->trust = LPK_OFFICIAL; snprintf(out->signer, sizeof out->signer, "LordOS"); }
        else {
            const char *a = catalog_author(out->key);
            out->trust = a ? LPK_VERIFIED : LPK_SIGNED;
            snprintf(out->signer, sizeof out->signer, "%s", a ? a : sname && sname[0] ? sname : out->key);
        }
        if (catalog_revoked(out->key, id, version, out->contents_sha, out->revoked_why, sizeof out->revoked_why)) out->revoked = 1;
    }
    json_free(&d);
    return r;
}

/* ---- catálogo ---- */

void catalog_free(Catalog *c) {
    free(c->authors);
    free(c->revoked_keys);
    free(c->revoked);
    free(c->packages);
    memset(c, 0, sizeof *c);
}

static void cpy(char *dst, size_t n, const char *s) { snprintf(dst, n, "%s", s ? s : ""); }

int catalog_parse(const char *json, size_t len, const char *sig, size_t siglen, Catalog *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    char key[65];
    if (sig_verify_detached(sig, siglen, json, len, key, err, errlen)) return -1;
    if (!sig_official(key)) { seterr(err, errlen, "el catálogo no está firmado por LordOS"); return -1; }
    JDoc d;
    char why[96];
    if (json_parse(json, len, &d, why, sizeof why)) { if (err && errlen) snprintf(err, errlen, "catalog.json inválido: %s", why); return -1; }
    const JVal *r = d.root;
    if (json_num(r, "format", 0) != 1) { json_free(&d); seterr(err, errlen, "catálogo de un formato desconocido"); return -1; }
    out->serial = json_num(r, "serial", 0);
    cpy(out->date, sizeof out->date, json_str(r, "date"));
    const JVal *a = json_get(r, "authors"), *rv = json_get(r, "revoked"), *p = json_get(r, "packages");
    const JVal *rk = json_get(rv, "keys"), *rp = json_get(rv, "packages");
    size_t na = a && a->type == J_ARR ? a->len : 0, nk = rk && rk->type == J_ARR ? rk->len : 0;
    size_t nr = rp && rp->type == J_ARR ? rp->len : 0, np = p && p->type == J_ARR ? p->len : 0;
    out->authors = calloc(na + 1, sizeof *out->authors);
    out->revoked_keys = calloc(nk + 1, sizeof *out->revoked_keys);
    out->revoked = calloc(nr + 1, sizeof *out->revoked);
    out->packages = calloc(np + 1, sizeof *out->packages);
    if (!out->authors || !out->revoked_keys || !out->revoked || !out->packages) { json_free(&d); catalog_free(out); seterr(err, errlen, "sin memoria"); return -1; }
    for (const JVal *x = na ? a->kid : NULL; x; x = x->next) {
        const char *k = json_str(x, "key"), *n = json_str(x, "name");
        if (!is_hex(k, 64) || !n) continue;
        cpy(out->authors[out->nauthors].key, 65, k);
        cpy(out->authors[out->nauthors++].name, 96, n);
    }
    for (const JVal *x = nk ? rk->kid : NULL; x; x = x->next)
        if (x->type == J_STR && is_hex(x->str, 64)) cpy(out->revoked_keys[out->nrevoked_keys++], 65, x->str);
    for (const JVal *x = nr ? rp->kid : NULL; x; x = x->next) {
        const char *id = json_str(x, "id"), *v = json_str(x, "version"), *sha = json_str(x, "sha256");
        if (!(id && lpk_valid_id(id)) && !is_hex(sha, 64)) continue;
        CatRevoked *e = &out->revoked[out->nrevoked++];
        cpy(e->id, sizeof e->id, id);
        cpy(e->version, sizeof e->version, v ? v : "*");
        cpy(e->sha, sizeof e->sha, is_hex(sha, 64) ? sha : "");
    }
    for (const JVal *x = np ? p->kid : NULL; x; x = x->next) {
        const char *id = json_str(x, "id"), *v = json_str(x, "version"), *sha = json_str(x, "sha256"), *k = json_str(x, "key");
        if (!id || !lpk_valid_id(id) || !v) continue;
        CatPackage *e = &out->packages[out->npackages++];
        cpy(e->id, sizeof e->id, id);
        cpy(e->version, sizeof e->version, v);
        cpy(e->sha256, sizeof e->sha256, is_hex(sha, 64) ? sha : "");
        cpy(e->key, sizeof e->key, is_hex(k, 64) ? k : "");
        e->size = json_num(x, "size", 0);
    }
    json_free(&d);
    out->ok = 1;
    return 0;
}

static char *slurp(const char *path, size_t max, size_t *len, struct stat *st) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    char *b = NULL;
    if (!fstat(fd, st) && S_ISREG(st->st_mode) && (size_t)st->st_size <= max && (b = malloc((size_t)st->st_size + 1))) {
        size_t got = 0;
        while (got < (size_t)st->st_size) {
            ssize_t r = read(fd, b + got, (size_t)st->st_size - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got != (size_t)st->st_size) { free(b); b = NULL; }
        else { b[got] = 0; *len = got; }
    }
    close(fd);
    return b;
}

static Catalog current;
static struct { long long mtime, size; } seen[2];

static int load_one(const char *json_path, Catalog *out, struct stat *st) {
    char sig_path[PATH_MAX];
    snprintf(sig_path, sizeof sig_path, "%.*s.sig", (int)(strlen(json_path) - 5), json_path);    /* catalog.json → catalog.sig */
    size_t jl = 0, sl = 0;
    struct stat ss;
    char *j = slurp(json_path, CAT_MAX, &jl, st), *s = j ? slurp(sig_path, 4096, &sl, &ss) : NULL;
    char err[128];
    int r = j && s ? catalog_parse(j, jl, s, sl, out, err, sizeof err) : -1;
    if (j && s && r) fprintf(stderr, "firma: %s: %s\n", json_path, err);
    free(j);
    free(s);
    if (!r) snprintf(out->path, sizeof out->path, "%s", json_path);
    return r;
}

const Catalog *catalog_current(void) {
    char user[PATH_MAX];
    const char *home = getenv("HOME");
    snprintf(user, sizeof user, "%s/.local/share/lordos/catalog/catalog.json", home ? home : "/nonexistent");
    const char *paths[2] = { "/usr/share/lordos/catalog.json", user };
    struct stat st[2];
    int changed = 0;
    for (int i = 0; i < 2; i++) {
        long long m = stat(paths[i], &st[i]) ? -1 : (long long)st[i].st_mtime * 1000000000LL + st[i].st_mtim.tv_nsec;
        long long sz = m < 0 ? -1 : (long long)st[i].st_size;
        if (m != seen[i].mtime || sz != seen[i].size) changed = 1;
        seen[i].mtime = m;
        seen[i].size = sz;
    }
    static int first = 1;
    if (changed || first) {
        first = 0;
        catalog_free(&current);
        for (int i = 0; i < 2; i++) {
            Catalog c;
            struct stat s2;
            if (load_one(paths[i], &c, &s2)) continue;
            if (!current.ok || c.serial > current.serial) { catalog_free(&current); current = c; }
            else catalog_free(&c);
        }
    }
    return current.ok ? &current : NULL;
}

const char *catalog_author(const char *keyhex) {
    const Catalog *c = catalog_current();
    for (int i = 0; c && i < c->nauthors; i++) if (!strcmp(c->authors[i].key, keyhex)) return c->authors[i].name;
    return NULL;
}

int catalog_revoked(const char *key, const char *id, const char *version, const char *contents_sha, char *why, size_t n) {
    const Catalog *c = catalog_current();
    if (!c) return 0;
    for (int i = 0; key && key[0] && i < c->nrevoked_keys; i++)
        if (!strcmp(c->revoked_keys[i], key)) { seterr(why, n, "LordOS revocó la clave con la que se firmó esta app"); return 1; }
    for (int i = 0; i < c->nrevoked; i++) {
        const CatRevoked *r = &c->revoked[i];
        int hit = r->sha[0] ? contents_sha && !strcmp(r->sha, contents_sha)
                            : id && !strcmp(r->id, id) && (!strcmp(r->version, "*") || (version && !strcmp(r->version, version)));
        if (hit) { seterr(why, n, "LordOS revocó esta versión de la app"); return 1; }
    }
    return 0;
}
