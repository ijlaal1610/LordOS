/* backup_test.c — prueba del motor de Copias de seguridad (src/backup/engine.c):
 * instantánea, enlaces duros entre instantáneas, enlaces simbólicos (nunca se siguen), exclusiones,
 * copia cortada y retomada, restaurar (mantener los dos, reemplazar, carpetas protegidas, escapes
 * por enlaces) y la retención (función pura, con fechas inventadas). */
/* las rutas de prueba son cortas: GCC no puede saberlo y avisa en cada snprintf de rutas */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "backup/engine.h"
#include "backup/crypt.h"
#include "fsops.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FALLA %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static char T[PATH_MAX], H[PATH_MAX], M[PATH_MAX];

static void put(const char *rel, const char *text) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", H, rel);
    FILE *f = fopen(p, "w");
    if (!f) { perror(p); exit(2); }
    fputs(text, f);
    fclose(f);
}

static void put_big(const char *rel, size_t mb) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s", H, rel);
    FILE *f = fopen(p, "w");
    if (!f) { perror(p); exit(2); }
    char *buf = malloc(1 << 20);
    for (size_t i = 0; i < mb; i++) { memset(buf, (int)('a' + i % 26), 1 << 20); fwrite(buf, 1, 1 << 20, f); }
    free(buf);
    fclose(f);
}

static void mk(const char *rel) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", H, rel); mkdir(p, 0755); }

static int slurp(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t k = fread(out, 1, n - 1, f);
    out[k] = 0;
    fclose(f);
    return 0;
}

static ino_t ino(const char *path) { struct stat st; return lstat(path, &st) ? 0 : st.st_ino; }
static int exists(const char *path) { struct stat st; return lstat(path, &st) == 0; }

static void snap_path(const char *id, const char *rel, char *out, size_t n) { snprintf(out, n, "%s/%s/%s", M, id, rel); }

static void init_run(BkRun *r) {
    memset(r, 0, sizeof *r);
    r->home = H;
    r->machine = M;
    r->excl[r->nexcl++] = ".cache";
    r->excl[r->nexcl++] = "Excluida";
    r->min_free = 1;
}

/* checkpoint que corta después de ~N bytes */
static uint64_t cut_after;
static int cut_cb(BkRun *r) { return cut_after && r->bytes >= cut_after; }

static void test_retention(void) {
    /* una por hora durante 90 días, terminando ahora */
    enum { N = 90 * 24 };
    static time_t t[N];
    static int keep[N];
    time_t now = 1792000000;                /* 2026 */
    for (int i = 0; i < N; i++) t[i] = now - (time_t)(N - 1 - i) * 3600;
    bk_retention(t, N, now, 0, keep);
    int recent = 0, total = 0;
    for (int i = 0; i < N; i++) { total += keep[i]; if (now - t[i] < 86400) { recent++; CHECK(keep[i], "las de las últimas 24 h quedan"); } }
    CHECK(recent == 24, "24 por hora (%d)", recent);
    CHECK(keep[N - 1], "la más nueva queda");
    /* entre 1 y 30 días: una por día calendario */
    int days[40] = { 0 };
    for (int i = 0; i < N; i++) {
        long age = (long)(now - t[i]);
        if (keep[i] && age >= 86400 && age < 30L * 86400) days[(t[i] / 86400) % 40]++;
    }
    for (int d = 0; d < 40; d++) CHECK(days[d] <= 1, "como mucho una por día (%d)", days[d]);
    /* después de 30 días: una por semana */
    long weeks[20];
    int nw = 0;
    for (int i = 0; i < N; i++) {
        if (!keep[i] || now - t[i] < 30L * 86400) continue;
        long w = (t[i] - 4L * 86400) / (7L * 86400);
        for (int k = 0; k < nw; k++) CHECK(weeks[k] != w, "dos de la misma semana");
        if (nw < 20) weeks[nw++] = w;
    }
    CHECK(nw >= 8 && nw <= 10, "semanales entre 30 y 90 días (%d)", nw);
    CHECK(total < 24 + 31 + 11, "total razonable (%d)", total);
    /* casos chicos */
    time_t one = now;
    int k1 = 0;
    bk_retention(&one, 1, now, 0, &k1);
    CHECK(k1 == 1, "una sola queda");
    time_t old[3] = { now - 100L * 86400, now - 99L * 86400, now - 98L * 86400 };   /* misma semana o no, la última queda */
    int k3[3];
    bk_retention(old, 3, now, 0, k3);
    CHECK(k3[2] == 1, "la más nueva, aunque sea vieja, queda");
    /* zona horaria: dos del mismo día local pero de distintos días UTC */
    time_t tz[2] = { 1791900000 - 3600 * 2, 1791900000 + 3600 * 2 };
    int kz[2];
    long off = -((1791900000L % 86400));     /* el corte de día local cae justo entre las dos */
    bk_retention(tz, 2, tz[1] + 5 * 86400, off + 86400 / 2, kz);
    CHECK(kz[1] == 1, "zona horaria: la última queda");
}

static void test_rel(void) {
    CHECK(bk_rel_valid(""), "vacío vale");
    CHECK(bk_rel_valid("a/b c/d.txt"), "normal vale");
    CHECK(!bk_rel_valid("/etc"), "absoluta no");
    CHECK(!bk_rel_valid("a/../b"), ".. no");
    CHECK(!bk_rel_valid(".."), ".. solo no");
    CHECK(!bk_rel_valid("a//b"), "// no");
    CHECK(!bk_rel_valid("a/./b"), ". no");
    CHECK(!bk_rel_valid("a/"), "barra final no");
    CHECK(!bk_rel_valid("a\nb"), "control no");
    CHECK(bk_id_valid("2026-10-17-235959"), "id");
    CHECK(!bk_id_valid("2026-10-17-23595"), "id corto");
    CHECK(!bk_id_valid("../10-17-235959x"), "id raro");
    char id[BK_ID_LEN];
    time_t now = time(NULL);
    bk_id_make(now, id);
    CHECK(bk_id_time(id) == now, "id ida y vuelta");
}

/* cifrado: tamaños, ida y vuelta, alteraciones, frase incorrecta y una instantánea cifrada */
static void test_crypt(void) {
    static const uint64_t sizes[] = { 0, 1, 15, 16, 65535, 65536, 65537, 131072, 200000, 1u << 20 };
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        uint64_t p = 99;
        CHECK(!bkc_plain_size(bkc_enc_size(sizes[i]), &p) && p == sizes[i], "tamaño %llu ida y vuelta", (unsigned long long)sizes[i]);
    }
    uint64_t junk;
    CHECK(bkc_plain_size(BKC_HDR + 5, &junk) != 0, "tamaño imposible rechazado");
    char M2[PATH_MAX], p[PATH_MAX], q[PATH_MAX], buf[256];
    snprintf(M2, sizeof M2, "%s/cifrado/" BK_DIRNAME "/equipo-usuario", T);
    snprintf(p, sizeof p, "%s/cifrado", T); mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/cifrado/" BK_DIRNAME, T); mkdir(p, 0700);
    mkdir(M2, 0700);
    uint8_t key[BKC_KEY], k2[BKC_KEY];
    CHECK(bkc_setup(M2, "una frase larga de prueba", key) == 0, "crear el cifrado");
    CHECK(bkc_unlock(M2, "una frase larga de prueba", k2) == 0 && !memcmp(key, k2, sizeof key), "desbloquear con la frase");
    CHECK(bkc_unlock(M2, "otra frase", k2) == -1, "frase incorrecta");
    /* ida y vuelta con varios tamaños por archivo */
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        snprintf(p, sizeof p, "%s/plano.bin", T); snprintf(q, sizeof q, "%s/cifrado.bin", T);
        FILE *f = fopen(p, "w");
        for (uint64_t k = 0; k < sizes[i]; k++) fputc((int)((k * 7 + i) & 0xff), f);
        fclose(f);
        int in = open(p, O_RDONLY), out = open(q, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int r = bkc_encrypt_fd(in, out, key, NULL, NULL);
        close(in); close(out);
        struct stat st;
        CHECK(r == 0 && !stat(q, &st) && (uint64_t)st.st_size == bkc_enc_size(sizes[i]), "cifrar %llu", (unsigned long long)sizes[i]);
        char back[PATH_MAX];
        snprintf(back, sizeof back, "%s/vuelta.bin", T);
        in = open(q, O_RDONLY); out = open(back, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        r = bkc_decrypt_fd(in, out, key, NULL, NULL);
        close(in); close(out);
        char cmd[3 * PATH_MAX];
        snprintf(cmd, sizeof cmd, "cmp -s '%s' '%s'", p, back);
        CHECK(r == 0 && system(cmd) == 0, "descifrar %llu igual", (unsigned long long)sizes[i]);
        if (sizes[i] == 200000) {
            /* un byte cambiado, un trozo menos y la clave equivocada: falla y no da datos falsos */
            int fd = open(q, O_RDWR);
            unsigned char b;
            pread(fd, &b, 1, 70000); b ^= 1; pwrite(fd, &b, 1, 70000);
            close(fd);
            in = open(q, O_RDONLY); out = open(back, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            CHECK(bkc_decrypt_fd(in, out, key, NULL, NULL) == -3, "byte alterado detectado");
            close(in); close(out);
            pread(fd = open(q, O_RDWR), &b, 1, 70000); b ^= 1; pwrite(fd, &b, 1, 70000);
            CHECK(ftruncate(fd, (off_t)bkc_enc_size(2 * 65536)) == 0, "cortar");
            close(fd);
            in = open(q, O_RDONLY); out = open(back, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            CHECK(bkc_decrypt_fd(in, out, key, NULL, NULL) == -3, "trozos de menos detectado");
            close(in); close(out);
            uint8_t bad[BKC_KEY];
            memcpy(bad, key, sizeof bad); bad[0] ^= 1;
            in = open(q, O_RDONLY); out = open(back, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            CHECK(bkc_decrypt_fd(in, out, bad, NULL, NULL) == -3, "clave equivocada detectada");
            close(in); close(out);
        }
    }
    /* instantánea cifrada: nada en claro, enlaces duros igual, restaurar descifra */
    BkRun r;
    memset(&r, 0, sizeof r);
    r.home = H; r.machine = M2; r.key = key; r.min_free = 1;
    r.excl[r.nexcl++] = "dest";
    put("secreto.txt", "texto muy secreto 1234\n");
    int rc = bk_snapshot(&r, time(NULL) - 600);
    CHECK(rc == 0, "instantánea cifrada (%s)", r.error);
    char id1[BK_ID_LEN];
    snprintf(id1, sizeof id1, "%s", r.id);
    snprintf(p, sizeof p, "%s/%s/secreto.txt", M2, id1);
    CHECK(!slurp(p, buf, sizeof buf) && !strstr(buf, "secreto 1234") && !memcmp(buf, "LBK1", 4), "el archivo de la copia está cifrado");
    memset(&r, 0, sizeof r);
    r.home = H; r.machine = M2; r.key = key; r.min_free = 1;
    r.excl[r.nexcl++] = "dest";
    rc = bk_snapshot(&r, time(NULL) - 300);
    CHECK(rc == 0 && r.copied == 0 && r.linked > 3, "cifrada: lo igual se enlaza (%llu copiados)", (unsigned long long)r.copied);
    snprintf(q, sizeof q, "%s/%s/secreto.txt", M2, r.id);
    CHECK(ino(p) == ino(q), "cifrada: mismo inodo");
    snprintf(q, sizeof q, "%s/secreto.txt", H);
    unlink(q);
    BkRestore o = { .key = key };
    rc = bk_restore(M2, id1, "secreto.txt", H, 0, " (restaurado)", &o);
    CHECK(rc == 0 && o.files == 1 && !slurp(q, buf, sizeof buf) && !strcmp(buf, "texto muy secreto 1234\n"), "restaurar descifra");
    rc = bk_restore(M2, id1, "secreto.txt", H, 0, " (restaurado)", &o);
    CHECK(rc == 0 && o.kept_both == 0 && o.skipped == 1, "cifrada: igual al actual, no se duplica");
    /* copia alterada: no se escribe nada */
    int fd = open(p, O_RDWR);
    unsigned char b;
    pread(fd, &b, 1, BKC_HDR + 2); b ^= 0x40; pwrite(fd, &b, 1, BKC_HDR + 2);
    close(fd);
    unlink(q);
    BkRestore o2 = { .key = key };
    bk_restore(M2, id1, "secreto.txt", H, 0, " (restaurado)", &o2);
    CHECK(o2.errors == 1 && !exists(q), "copia alterada: error y nada escrito (%s)", o2.error);
    bkc_wipe(key, sizeof key);
}

int main(void) {
    test_retention();
    test_rel();

    snprintf(T, sizeof T, "/tmp/lordos-backup-test-%d", (int)getpid());
    mkdir(T, 0700);
    char h0[PATH_MAX];
    snprintf(h0, sizeof h0, "%s/home", T);
    mkdir(h0, 0755);
    if (!realpath(h0, H)) return 2;
    snprintf(M, sizeof M, "%s/dest/" BK_DIRNAME "/equipo-usuario", H);   /* destino ADENTRO de la carpeta personal */
    fsops_init(H, "usuario-de-prueba");

    put("a.txt", "versión 1\n");
    mk("dir"); put_big("dir/b.bin", 1);
    mk("sub"); mk("sub/deep"); put("sub/deep/c.txt", "c\n");
    mk(".cache"); put(".cache/basura", "x");
    mk("Excluida"); put("Excluida/x", "x");
    mk(".config"); mk(".config/lordos"); put(".config/lordos/settings.json", "{\"language\":\"es\"}\n");
    char p[PATH_MAX], q[PATH_MAX], buf[256];
    snprintf(p, sizeof p, "%s/escape", H); symlink("/etc/passwd", p);
    snprintf(p, sizeof p, "%s/relativo", H); symlink("a.txt", p);
    snprintf(p, sizeof p, "%s/fifo", H); mkfifo(p, 0600);

    time_t t0 = time(NULL) - 7200;
    BkRun r;
    /* el destino adentro de home: se saltea por dev/ino */
    char skip[PATH_MAX], d0[PATH_MAX];
    snprintf(d0, sizeof d0, "%s/dest/" BK_DIRNAME, H);
    init_run(&r);
    r.skip_real = d0;
    {
        char mkp[PATH_MAX];
        snprintf(mkp, sizeof mkp, "%s/dest", H); mkdir(mkp, 0700); mkdir(d0, 0700);
        if (realpath(d0, skip)) r.skip_real = skip;
    }
    int rc = bk_snapshot(&r, t0);
    CHECK(rc == 0, "primera instantánea (%d: %s)", rc, r.error);
    char id1[BK_ID_LEN];
    snprintf(id1, sizeof id1, "%s", r.id);
    snap_path(id1, "a.txt", p, sizeof p);
    CHECK(!slurp(p, buf, sizeof buf) && !strcmp(buf, "versión 1\n"), "a.txt copiado");
    snap_path(id1, "sub/deep/c.txt", p, sizeof p);
    CHECK(exists(p), "carpetas profundas");
    snap_path(id1, ".cache", p, sizeof p);
    CHECK(!exists(p), ".cache excluida");
    snap_path(id1, "Excluida", p, sizeof p);
    CHECK(!exists(p), "exclusión del usuario");
    snap_path(id1, "fifo", p, sizeof p);
    CHECK(!exists(p), "fifo salteado");
    snap_path(id1, "dest", p, sizeof p);
    snprintf(q, sizeof q, "%s/" BK_DIRNAME, p);
    CHECK(!exists(q), "el destino no se copia a sí mismo");
    snap_path(id1, "escape", p, sizeof p);
    struct stat st;
    CHECK(!lstat(p, &st) && S_ISLNK(st.st_mode), "enlace guardado como enlace");
    ssize_t k = readlink(p, buf, sizeof buf - 1);
    CHECK(k > 0 && (buf[k] = 0, !strcmp(buf, "/etc/passwd")), "con el mismo texto");
    CHECK(r.skipped >= 1, "especiales contados (%llu)", (unsigned long long)r.skipped);

    /* segunda: cambia a.txt, el resto se enlaza */
    put("a.txt", "versión 2, más larga\n");
    init_run(&r);
    r.skip_real = skip;
    rc = bk_snapshot(&r, t0 + 3600);
    CHECK(rc == 0, "segunda instantánea (%s)", r.error);
    char id2[BK_ID_LEN];
    snprintf(id2, sizeof id2, "%s", r.id);
    CHECK(strcmp(id1, id2) < 0, "ids en orden");
    snap_path(id1, "dir/b.bin", p, sizeof p);
    snap_path(id2, "dir/b.bin", q, sizeof q);
    CHECK(ino(p) && ino(p) == ino(q), "dedup: mismo inodo");
    snap_path(id1, "a.txt", p, sizeof p);
    snap_path(id2, "a.txt", q, sizeof q);
    CHECK(ino(p) != ino(q), "cambiado: copia nueva");
    CHECK(r.copied == 1, "solo se copió uno (%llu)", (unsigned long long)r.copied);
    CHECK(r.linked >= 3, "enlazados (%llu)", (unsigned long long)r.linked);

    /* copia cortada y retomada */
    put("aaa-nuevo.txt", "nuevo antes del corte\n");
    put_big("zz-grande.bin", 40);
    init_run(&r);
    r.skip_real = skip;
    r.checkpoint = cut_cb;
    cut_after = 12u << 20;
    rc = bk_snapshot(&r, t0 + 5400);
    CHECK(rc == 1, "cortada (%d)", rc);
    char inprog[PATH_MAX];
    snprintf(inprog, sizeof inprog, "%s/.inprogress-%s", M, r.id);
    CHECK(exists(inprog), "queda .inprogress");
    char ids[16][BK_ID_LEN];
    CHECK(bk_list(M, ids, 16) == 2, "la cortada no figura");
    snprintf(p, sizeof p, "%s/zz-grande.bin", inprog);
    CHECK(!exists(p), "el archivo a medias no tiene nombre final");
    snprintf(p, sizeof p, "%s/aaa-nuevo.txt", inprog);
    ino_t copied_before = ino(p);
    CHECK(copied_before != 0, "lo de antes del corte está");
    cut_after = 0;
    init_run(&r);
    r.skip_real = skip;
    rc = bk_snapshot(&r, t0 + 5500);
    CHECK(rc == 0, "retomada (%s)", r.error);
    char id3[BK_ID_LEN];
    snprintf(id3, sizeof id3, "%s", r.id);
    snap_path(id3, "aaa-nuevo.txt", p, sizeof p);
    CHECK(ino(p) == copied_before, "retomada: lo ya copiado se enlaza (no se copia de nuevo)");
    CHECK(r.copied == 1, "retomada: solo el grande (%llu)", (unsigned long long)r.copied);
    snap_path(id3, "zz-grande.bin", p, sizeof p);
    CHECK(!stat(p, &st) && st.st_size == 40 << 20, "el grande completo");
    CHECK(!exists(inprog), "sin .inprogress");
    DIR *dd = opendir(M);
    int junk = 0;
    for (struct dirent *e; dd && (e = readdir(dd));) if (e->d_name[0] == '.' && strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) junk++;
    if (dd) closedir(dd);
    CHECK(junk == 0, "sin restos de la copia cortada (%d)", junk);

    /* restaurar */
    BkRestore o = { 0 };
    snprintf(p, sizeof p, "%s/a.txt", H);
    unlink(p);
    rc = bk_restore(M, id1, "a.txt", H, 0, " (restaurado)", &o);
    CHECK(rc == 0 && o.files == 1, "restaurar un archivo borrado (%d %s)", rc, o.error);
    CHECK(!slurp(p, buf, sizeof buf) && !strcmp(buf, "versión 1\n"), "con el contenido de entonces");
    put("a.txt", "versión 3 actual\n");
    rc = bk_restore(M, id1, "a.txt", H, 0, " (restaurado)", &o);
    snprintf(q, sizeof q, "%s/a (restaurado).txt", H);
    CHECK(rc == 0 && o.kept_both == 1 && !slurp(q, buf, sizeof buf) && !strcmp(buf, "versión 1\n"), "mantener los dos");
    CHECK(!slurp(p, buf, sizeof buf) && !strcmp(buf, "versión 3 actual\n"), "el actual no se pisa");
    rc = bk_restore(M, id1, "a.txt", H, 0, " (restaurado)", &o);
    snprintf(q, sizeof q, "%s/a (restaurado) 2.txt", H);
    CHECK(rc == 0 && exists(q), "segundo duplicado numerado");
    rc = bk_restore(M, id1, "a.txt", H, 1, " (restaurado)", &o);
    CHECK(rc == 0 && o.replaced == 1 && !slurp(p, buf, sizeof buf) && !strcmp(buf, "versión 1\n"), "reemplazar");
    /* una carpeta que ya no está */
    snprintf(p, sizeof p, "%s/sub/deep/c.txt", H); unlink(p);
    snprintf(p, sizeof p, "%s/sub/deep", H); rmdir(p);
    rc = bk_restore(M, id2, "sub/deep/c.txt", H, 0, " (restaurado)", &o);
    snprintf(p, sizeof p, "%s/sub/deep/c.txt", H);
    CHECK(rc == 0 && exists(p), "restaurar crea las carpetas que faltan");
    /* protegidas: un .desktop plantado en la copia y los ajustes no se escriben */
    char plant[PATH_MAX];
    snap_path(id3, ".local", plant, sizeof plant); chmod(plant, 0700); mkdir(plant, 0700);
    snap_path(id3, ".local/share", plant, sizeof plant); mkdir(plant, 0700);
    snap_path(id3, ".local/share/applications", plant, sizeof plant); mkdir(plant, 0700);
    snap_path(id3, ".local/share/applications/malo.desktop", plant, sizeof plant);
    FILE *f = fopen(plant, "w"); if (f) { fputs("[Desktop Entry]\nExec=rm -rf ~\n", f); fclose(f); }
    put(".config/lordos/settings.json", "{\"language\":\"pt\"}\n");
    rc = bk_restore(M, id3, "", H, 1, " (restaurado)", &o);
    CHECK(rc == 0, "restaurar todo (%s)", o.error);
    snprintf(p, sizeof p, "%s/.local/share/applications/malo.desktop", H);
    CHECK(!exists(p), "no escribe .desktop");
    snprintf(p, sizeof p, "%s/.config/lordos/settings.json", H);
    CHECK(!slurp(p, buf, sizeof buf) && strstr(buf, "pt"), "no pisa settings.json");
    CHECK(o.skipped >= 2, "protegidas salteadas (%llu)", (unsigned long long)o.skipped);
    /* enlaces: se restaura el enlace, no lo apuntado; no se baja por un enlace de la copia */
    snprintf(p, sizeof p, "%s/escape", H); unlink(p);
    rc = bk_restore(M, id1, "escape", H, 0, " (restaurado)", &o);
    CHECK(rc == 0 && !lstat(p, &st) && S_ISLNK(st.st_mode), "enlace restaurado como enlace");
    snap_path(id1, "afuera", plant, sizeof plant);
    symlink("/etc", plant);
    rc = bk_restore(M, id1, "afuera/passwd", H, 0, " (restaurado)", &o);
    snprintf(p, sizeof p, "%s/afuera", H);
    CHECK(rc != 0 && !exists(p), "no se escapa por un enlace de la copia");
    rc = bk_restore(M, id1, "../../../etc/passwd", H, 0, " (restaurado)", &o);
    CHECK(rc != 0, ".. rechazado");
    rc = bk_restore(M, id1, "a.txt", "/etc", 0, " (restaurado)", &o);
    CHECK(rc != 0, "destino fuera de las raíces rechazado");
    rc = bk_restore(M, id1, "a.txt", H, 0, "/x", &o);
    CHECK(rc != 0, "sufijo con barra rechazado");
    /* un enlace en la carpeta personal que apunta a una carpeta protegida */
    snprintf(p, sizeof p, "%s/atajo", H);
    snprintf(q, sizeof q, "%s/.local/share/applications", H);
    mkdir(q, 0700);
    symlink(q, p);
    snap_path(id1, "atajo", plant, sizeof plant); unlink(plant); mkdir(plant, 0700);
    snap_path(id1, "atajo/x.desktop", plant, sizeof plant);
    f = fopen(plant, "w"); if (f) { fputs("x", f); fclose(f); }
    rc = bk_restore(M, id1, "atajo/x.desktop", H, 0, " (restaurado)", &o);
    snprintf(p, sizeof p, "%s/x.desktop", q);
    CHECK(!exists(p), "no escribe en una protegida a través de un enlace (%d)", rc);

    /* poda: sin lugar, queda solo la más nueva */
    int before = bk_list(M, ids, 16);
    bk_prune(M, time(NULL), UINT64_MAX);
    int after = bk_list(M, ids, 16);
    CHECK(before == 3 && after == 1 && !strcmp(ids[0], id3), "poda por lugar: queda la última (%d → %d)", before, after);
    CHECK(bk_delete(M, "../../x") != 0, "borrar con nombre inválido");

    test_crypt();

    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof cmd, "chmod -R u+w '%s' && rm -rf '%s'", T, T);
    if (system(cmd)) { /* queda en /tmp */ }
    printf("backup-test: %d comprobaciones, %d fallas\n", checks, fails);
    return fails ? 1 : 0;
}
