/* fido_e2e.c — el cliente CTAP2 de punta a punta contra el autenticador por software
 * (../tests/fido-soft/softfido.c, por uhid). Lo corre tests/fido-soft/run-pw-116.sh con sudo.
 * Solo prueba contra softfido: una llave real (YubiKey, SoloKey, Nitrokey) NO está probada acá. */
#include "vault/ctap2.h"
#include "vault/fidodev.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int fails, total;
static void check(int ok, const char *what) { total++; printf("%s %s\n", ok ? "ok   " : "FALLA", what); if (!ok) fails++; }

static int touches;
static void on_status(void *ud, int st) { (void)ud; if (st == FIDO_ST_TOUCH) touches++; }

static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static int set_mode(int fd, uint8_t m) {
    uint8_t r[8];
    size_t n = 0;
    return ctaphid_command(fd, 0xc1, &m, 1, r, sizeof r, &n, 2000);
}

static int cancel_fd = -1;
static void *canceller(void *a) { (void)a; usleep(500000); fido_cancel(cancel_fd); return NULL; }

int main(void) {
    FidoDev devs[8];
    int n = fidodev_list(devs, 8), soft = -1;
    for (int i = 0; i < n; i++) {
        printf("llave: %s «%s» %04x:%04x\n", devs[i].path, devs[i].name, devs[i].vid, devs[i].pid);
        if (strstr(devs[i].name, "LordOS Soft FIDO2")) soft = i;
    }
    check(soft >= 0, "fidodev_list encuentra el autenticador por software");
    if (soft < 0) return 1;
    check(devs[soft].vid == 0x1209 && devs[soft].pid == 0x5f1d, "fidodev: vid:pid de uevent");
    int fd = fidodev_open(devs[soft].path);
    check(fd >= 0, "fidodev_open");
    if (fd < 0) return 1;

    FidoInfo info;
    check(fido_get_info(fd, &info) == 0, "getInfo");
    check(info.hmac_secret == 1 && info.client_pin == 0, "getInfo: hmac-secret, sin PIN");

    uint8_t cred[FIDO_CRED_MAX], cred2[FIDO_CRED_MAX];
    size_t cl = 0, cl2 = 0;
    touches = 0;
    int rc = fido_make_credential(fd, NULL, cred, &cl, 10000, on_status, NULL);
    check(rc == 0 && cl > 0, "makeCredential con hmac-secret");
    check(touches > 0, "makeCredential: avisó «tocá la llave»");

    uint8_t saltA[32], saltB[32], o1[32], o2[32], o3[32];
    for (int i = 0; i < 32; i++) { saltA[i] = (uint8_t)i; saltB[i] = (uint8_t)(255 - i); }
    long long t0 = now_ms();
    rc = fido_hmac_secret(fd, NULL, cred, cl, saltA, o1, 10000, on_status, NULL);
    printf("hmac-secret: %lld ms (con 3 «toques» simulados de 100 ms)\n", now_ms() - t0);
    check(rc == 0, "hmac-secret salt A");
    check(fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 10000, NULL, NULL) == 0 && !memcmp(o1, o2, 32), "hmac-secret: mismo salt → mismo secreto");
    check(fido_hmac_secret(fd, NULL, cred, cl, saltB, o3, 10000, NULL, NULL) == 0 && memcmp(o1, o3, 32), "hmac-secret: otro salt → otro secreto");

    check(fido_make_credential(fd, NULL, cred2, &cl2, 10000, NULL, NULL) == 0, "segunda credencial");
    check(fido_hmac_secret(fd, NULL, cred2, cl2, saltA, o2, 10000, NULL, NULL) == 0 && memcmp(o1, o2, 32), "otra credencial → otro secreto");

    uint8_t bad[FIDO_CRED_MAX];
    memcpy(bad, cred, cl);
    bad[cl / 2] ^= 0x40;
    rc = fido_hmac_secret(fd, NULL, bad, cl, saltA, o2, 10000, NULL, NULL);
    check(rc == FIDO_ERR_NO_CREDENTIAL, "credencial ajena → FIDO_ERR_NO_CREDENTIAL");

    check(set_mode(fd, 1) == 0, "modo: nadie toca");
    t0 = now_ms();
    rc = fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 1200, NULL, NULL);
    long long dt = now_ms() - t0;
    check(rc == FIDO_ERR_TIMEOUT && dt >= 1100 && dt < 3500, "sin toque → FIDO_ERR_TIMEOUT a tiempo");
    printf("  (tardó %lld ms con plazo de 1200)\n", dt);

    cancel_fd = fd;
    pthread_t th;
    pthread_create(&th, NULL, canceller, NULL);
    t0 = now_ms();
    rc = fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 10000, NULL, NULL);
    pthread_join(th, NULL);
    check(rc == FIDO_ERR_CANCELLED && now_ms() - t0 < 3000, "fido_cancel desde otro hilo → FIDO_ERR_CANCELLED");

    rc = set_mode(fd, 2);
    if (rc) printf("  set_mode: %s\n", fido_strerror(rc));
    check(rc == 0, "modo: el usuario rechaza");
    check(fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 5000, NULL, NULL) == FIDO_ERR_DENIED, "rechazo → FIDO_ERR_DENIED");
    set_mode(fd, 0);
    check(fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 5000, NULL, NULL) == 0 && !memcmp(o1, o2, 32), "después de todo eso, vuelve a dar el mismo secreto");

    /* PIN */
    check(fido_set_pin(fd, "12") == FIDO_ERR_PIN_INVALID, "setPIN: muy corto se rechaza (cliente)");
    check(fido_set_pin(fd, "1234") == 0, "setPIN 1234");
    check(fido_get_info(fd, &info) == 0 && info.client_pin == 1 && info.pin_retries == 8, "getInfo: con PIN, 8 reintentos");
    check(fido_set_pin(fd, "5678") == FIDO_ERR_DENIED, "setPIN otra vez → rechazado (ya tiene)");
    check(fido_make_credential(fd, NULL, cred2, &cl2, 5000, NULL, NULL) == FIDO_ERR_PIN_REQUIRED, "makeCredential sin PIN → FIDO_ERR_PIN_REQUIRED");
    check(fido_make_credential(fd, "0000", cred2, &cl2, 5000, NULL, NULL) == FIDO_ERR_PIN_INVALID, "PIN equivocado → FIDO_ERR_PIN_INVALID");
    check(fido_get_info(fd, &info) == 0 && info.pin_retries == 7, "un reintento menos");
    check(fido_make_credential(fd, "1234", cred2, &cl2, 5000, NULL, NULL) == 0, "makeCredential con PIN");
    check(fido_get_info(fd, &info) == 0 && info.pin_retries == 8, "PIN bien: reintentos de vuelta a 8");
    check(fido_hmac_secret(fd, "1234", cred2, cl2, saltA, o2, 5000, NULL, NULL) == 0, "hmac-secret con PIN (UV)");
    check(fido_hmac_secret(fd, NULL, cred2, cl2, saltA, o3, 5000, NULL, NULL) == 0, "hmac-secret de la misma credencial sin PIN (solo toque)");
    printf("  (softfido es CTAP 2.0: con y sin UV dan %s)\n", memcmp(o2, o3, 32) ? "distinto" : "lo mismo");
    check(fido_hmac_secret(fd, NULL, cred, cl, saltA, o2, 5000, NULL, NULL) == 0 && !memcmp(o1, o2, 32), "la credencial de antes del PIN sigue dando lo mismo");
    check(fido_hmac_secret(fd, "9999", cred, cl, saltA, o2, 5000, NULL, NULL) == FIDO_ERR_PIN_INVALID, "hmac-secret con PIN equivocado → FIDO_ERR_PIN_INVALID");

    close(fd);
    printf("fido-e2e: %d/%d\n", total - fails, total);
    return fails ? 1 : 0;
}
