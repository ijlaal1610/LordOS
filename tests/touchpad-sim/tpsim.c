/* tpsim.c — touchpad simulado con uinput, para probar el driver de LordOS (server/src/tp) en una
 * VM sin touchpad. Crea un clickpad parecido a un Synaptics de notebook (gestures.h) y reproduce
 * gestos guionados con sus tiempos reales. Ver docs/TRACKPAD.md.
 *
 *   tpsim list                          los gestos
 *   tpsim dump GESTO...                 la secuencia evdev en texto ("ms tipo código valor")
 *   sudo tpsim play [--touchscreen] [--wait S] GESTO...
 *                                       crea el dispositivo, espera S segundos (2) y los reproduce
 *   sudo tpsim serve [--touchscreen] FIFO
 *                                       crea el dispositivo y queda esperando nombres de gestos
 *                                       (uno o más por línea) en FIFO; "pause MS" espera, "quit" sale
 *   sudo tpsim play --mouse [--wait S] wheel-up|wheel-down|hwheel-left|hwheel-right|mouse-move...
 *                                       un mouse con rueda de alta resolución (y horizontal)
 *   sudo tpsim record /dev/input/eventN SEGUNDOS
 *                                       graba un dispositivo real (para tp-test --replay)
 *
 * Compilar: cc -O2 -Wall -Wextra -o tpsim tpsim.c gestures.c -lm */
#include "gestures.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double mono(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void sleep_ms(double ms) {
    if (ms <= 0) return;
    struct timespec t = { (time_t)(ms / 1000), (long)((ms - (time_t)(ms / 1000) * 1000) * 1e6) };
    while (nanosleep(&t, &t) && errno == EINTR) {}
}

static void dump_emit(void *ud, double t, int type, int code, int value) {
    (void)ud;
    printf("%.1f %d %d %d\n", t, type, code, value);
}

typedef struct { int fd; double t0; } Live;

static void live_emit(void *ud, double t, int type, int code, int value) {
    Live *l = ud;
    sleep_ms(l->t0 + t - mono());
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    if (write(l->fd, &ev, sizeof ev) != sizeof ev) perror("tpsim: write");
}

static void abs_setup(int fd, int code, int min, int max, int res) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = (unsigned short)code;
    a.absinfo.minimum = min;
    a.absinfo.maximum = max;
    a.absinfo.resolution = res;
    ioctl(fd, UI_SET_ABSBIT, code);
    if (ioctl(fd, UI_ABS_SETUP, &a)) perror("tpsim: UI_ABS_SETUP");
}

static int create(int touchscreen) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("tpsim: /dev/uinput (¿sudo?)"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int keys[] = { BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP };
    for (size_t i = touchscreen ? 1 : 0; i < (touchscreen ? 2 : sizeof keys / sizeof keys[0]); i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    abs_setup(fd, ABS_X, SIM_X_MIN, SIM_X_MAX, SIM_RES);
    abs_setup(fd, ABS_Y, SIM_Y_MIN, SIM_Y_MAX, SIM_RES);
    abs_setup(fd, ABS_PRESSURE, 0, SIM_PMAX, 0);
    abs_setup(fd, ABS_MT_SLOT, 0, SIM_SLOTS - 1, 0);
    abs_setup(fd, ABS_MT_TRACKING_ID, 0, 65535, 0);
    abs_setup(fd, ABS_MT_POSITION_X, SIM_X_MIN, SIM_X_MAX, SIM_RES);
    abs_setup(fd, ABS_MT_POSITION_Y, SIM_Y_MIN, SIM_Y_MAX, SIM_RES);
    abs_setup(fd, ABS_MT_PRESSURE, 0, SIM_PMAX, 0);
    if (touchscreen) ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    else { ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER); ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD); }
    struct uinput_setup u;
    memset(&u, 0, sizeof u);
    u.id.bustype = touchscreen ? BUS_USB : BUS_I8042;
    u.id.vendor = touchscreen ? 0x0eef : 0x0002;
    u.id.product = touchscreen ? 0x0001 : 0x0007;
    snprintf(u.name, sizeof u.name, "%s", touchscreen ? "LordOS Pantalla táctil (simulada)" : SIM_NAME);
    if (ioctl(fd, UI_DEV_SETUP, &u) || ioctl(fd, UI_DEV_CREATE)) { perror("tpsim: crear dispositivo"); close(fd); return -1; }
    char sys[64] = "";
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sys), sys) >= 0) fprintf(stderr, "tpsim: %s creado (/sys/devices/virtual/input/%s)\n", u.name, sys);
    return fd;
}

/* ---- mouse con rueda de alta resolución (REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES) ---- */

static int create_mouse(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("tpsim: /dev/uinput (¿sudo?)"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    int keys[] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
    for (size_t i = 0; i < 3; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    int rels[] = { REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES };
    for (size_t i = 0; i < 6; i++) ioctl(fd, UI_SET_RELBIT, rels[i]);
    struct uinput_setup u;
    memset(&u, 0, sizeof u);
    u.id.bustype = BUS_USB;
    u.id.vendor = 0x046d;
    u.id.product = 0x4082;
    snprintf(u.name, sizeof u.name, "LordOS mouse con rueda de alta resolución (simulado)");
    if (ioctl(fd, UI_DEV_SETUP, &u) || ioctl(fd, UI_DEV_CREATE)) { perror("tpsim: crear mouse"); close(fd); return -1; }
    fprintf(stderr, "tpsim: %s creado\n", u.name);
    return fd;
}

static void rel(int fd, int code, int value) {
    struct input_event ev[2];
    memset(ev, 0, sizeof ev);
    ev[0].type = EV_REL; ev[0].code = (unsigned short)code; ev[0].value = value;
    ev[1].type = EV_SYN; ev[1].code = SYN_REPORT;
    if (write(fd, ev, sizeof ev) != sizeof ev) perror("tpsim: write");
}

/* Una rueda "libre" de alta resolución: cada paso (120) llega en 4 de 30, y con el último el paso
 * entero de siempre (REL_WHEEL), como hace el kernel. */
static void play_mouse(int fd, char **names, int n) {
    for (int i = 0; i < n; i++) {
        const char *g = names[i];
        int vert = !strncmp(g, "wheel", 5), sign = strstr(g, "up") || strstr(g, "right") ? 1 : -1;
        if (!strcmp(g, "mouse-move")) { for (int k = 0; k < 30; k++) { rel(fd, REL_X, 8); sleep_ms(8); } }
        else if (vert || !strncmp(g, "hwheel", 6)) {
            for (int step = 0; step < 3; step++)
                for (int k = 0; k < 4; k++) {
                    struct input_event ev[3];
                    memset(ev, 0, sizeof ev);
                    int m = 0;
                    ev[m].type = EV_REL; ev[m].code = vert ? REL_WHEEL_HI_RES : REL_HWHEEL_HI_RES; ev[m++].value = 30 * sign;
                    if (k == 3) { ev[m].type = EV_REL; ev[m].code = vert ? REL_WHEEL : REL_HWHEEL; ev[m++].value = sign; }
                    ev[m].type = EV_SYN; ev[m++].code = SYN_REPORT;
                    if (write(fd, ev, sizeof ev[0] * (size_t)m) < 0) perror("tpsim: write");
                    sleep_ms(15);
                }
        } else { fprintf(stderr, "tpsim: gesto de mouse desconocido: %s\n", g); continue; }
        fprintf(stderr, "tpsim: %s\n", g);
    }
}

static int play(int fd, char **names, int n) {
    for (int i = 0; i < n; i++) {
        const SimGesture *g = sim_find(names[i]);
        if (!g) { fprintf(stderr, "tpsim: gesto desconocido: %s\n", names[i]); continue; }
        Live l = { fd, mono() };
        SimGen gen;
        sim_init(&gen, live_emit, &l);
        double t0 = mono();
        g->play(&gen);
        sleep_ms(l.t0 + gen.t - mono());
        fprintf(stderr, "tpsim: %s (%s) en %.0f ms\n", g->name, g->what, mono() - t0);
    }
    return 0;
}

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: tpsim list | dump GESTO... | play [--touchscreen] [--wait S] GESTO... | serve [--touchscreen] FIFO | record DEV SEG\n");
        return 2;
    }
    if (!strcmp(argv[1], "list")) {
        int n;
        const SimGesture *g = sim_gestures(&n);
        for (int i = 0; i < n; i++) printf("%-14s %s\n", g[i].name, g[i].what);
        return 0;
    }
    if (!strcmp(argv[1], "dump")) {
        printf("# tpsim %s: %.0f x %.0f mm, %d contactos, resolución %d\n", SIM_NAME, SIM_W_MM, SIM_H_MM, SIM_SLOTS, SIM_RES);
        double t = 0;
        for (int i = 2; i < argc; i++) {
            const SimGesture *g = sim_find(argv[i]);
            if (!g) { fprintf(stderr, "tpsim: gesto desconocido: %s\n", argv[i]); return 1; }
            SimGen gen;
            sim_init(&gen, dump_emit, NULL);
            gen.t = t;
            g->play(&gen);
            t = gen.t + 200;
        }
        return 0;
    }
    if (!strcmp(argv[1], "record") && argc >= 4) {
        int fd = open(argv[2], O_RDONLY | O_CLOEXEC);
        if (fd < 0) { perror(argv[2]); return 1; }
        int clk = CLOCK_MONOTONIC;
        ioctl(fd, EVIOCSCLOCKID, &clk);
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        struct input_absinfo x, y;
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x);
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y);
        printf("# record %s\n# caps x %d %d %d y %d %d %d\n", name, x.minimum, x.maximum, x.resolution, y.minimum, y.maximum, y.resolution);
        double end = mono() + atof(argv[3]) * 1000, t0 = -1;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        while (mono() < end) {
            struct input_event ev;
            ssize_t r = read(fd, &ev, sizeof ev);
            if (r != sizeof ev) { sleep_ms(2); continue; }
            double t = ev.input_event_sec * 1e3 + ev.input_event_usec / 1e3;
            if (t0 < 0) t0 = t;
            printf("%.1f %d %d %d\n", t - t0, ev.type, ev.code, ev.value);
        }
        return 0;
    }
    int touchscreen = 0, mouse = 0;
    double wait = 2;
    int i = 2;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "--touchscreen")) touchscreen = 1;
        else if (!strcmp(argv[i], "--mouse")) mouse = 1;
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) wait = atof(argv[++i]);
    }
    if (!strcmp(argv[1], "play")) {
        int fd = mouse ? create_mouse() : create(touchscreen);
        if (fd < 0) return 1;
        sleep_ms(wait * 1000);
        if (mouse) play_mouse(fd, argv + i, argc - i);
        else play(fd, argv + i, argc - i);
        sleep_ms(300);
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return 0;
    }
    if (!strcmp(argv[1], "serve") && i < argc) {
        const char *fifo = argv[i];
        unlink(fifo);
        if (mkfifo(fifo, 0666)) { perror(fifo); return 1; }
        chmod(fifo, 0666);
        int fd = create(touchscreen);
        if (fd < 0) return 1;
        signal(SIGINT, on_sig);
        signal(SIGTERM, on_sig);
        while (!stop) {
            FILE *f = fopen(fifo, "r");             /* se bloquea hasta que alguien escribe */
            if (!f) break;
            char line[512];
            while (!stop && fgets(line, sizeof line, f)) {
                char *names[32];
                int n = 0;
                for (char *tok = strtok(line, " \t\r\n"); tok && n < 32; tok = strtok(NULL, " \t\r\n")) names[n++] = tok;
                for (int k = 0; k < n; k++) {
                    if (!strcmp(names[k], "quit")) stop = 1;
                    else if (!strcmp(names[k], "pause") && k + 1 < n) sleep_ms(atof(names[++k]));
                    else play(fd, names + k, 1);
                }
            }
            fclose(f);
        }
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        unlink(fifo);
        return 0;
    }
    fprintf(stderr, "tpsim: orden desconocida\n");
    return 2;
}
