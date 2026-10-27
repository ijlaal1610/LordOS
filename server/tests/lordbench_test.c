/* lordbench_test.c — LordBench without the helper process (docs/BENCHMARK.md, "0.8.6 — LordBench"): the
 * records lord-server accepts from lord-bench, short runs of every CPU, memory and storage workload, the
 * conditions read from fake sysfs trees (temperatures and trip points, throttle counters, the Pi's
 * flags, batteries) with the thermal and battery guards, and the boot history per version. */
#include "../src/bench/lbproto.h"
#include "../src/bench/lbwork.h"
#include "../src/bench/lbcond.h"
#include "../src/bench/lbrun.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, oks;
#define CHECK(c, ...) do { if (c) oks++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char root[256];
static void put(const char *rel, const char *text) {
    char p[512];
    snprintf(p, sizeof p, "%s%s", root, rel);
    for (char *s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) { *s = 0; mkdir(p, 0755); *s = '/'; }
    FILE *f = fopen(p, "w");
    if (!f) { printf("can't write %s\n", p); exit(1); }
    fputs(text, f);
    fclose(f);
}

static LbMsg msg(uint32_t kind, uint32_t id, double v, double aux) { LbMsg m = { LB_MAGIC, kind, id, 0, v, aux }; return m; }

int main(void) {
    /* ---- records ---- */
    LbMsg m = msg(LB_MSG_RESULT, LB_CPU_SHA1, 1200, 15);
    CHECK(lb_msg_valid(&m), "a good result");
    m.magic = 1;
    CHECK(!lb_msg_valid(&m), "bad magic");
    m = msg(LB_MSG_RESULT, LB_COUNT, 1, 0);
    CHECK(!lb_msg_valid(&m), "id out of range");
    m = msg(LB_MSG_RESULT, LB_CPU_SHA1, NAN, 0);
    CHECK(!lb_msg_valid(&m), "NaN");
    m = msg(LB_MSG_RESULT, LB_CPU_SHA1, -5, 0);
    CHECK(!lb_msg_valid(&m), "a negative rate");
    m = msg(LB_MSG_RESULT, LB_CPU_SHA1, INFINITY, 0);
    CHECK(!lb_msg_valid(&m), "infinity");
    m = msg(LB_MSG_SKIP, LB_GPU_SCENE, 0, LB_SKIP_NODEV);
    CHECK(lb_msg_valid(&m), "a skip");
    m = msg(LB_MSG_SKIP, LB_GPU_SCENE, 0, 99);
    CHECK(!lb_msg_valid(&m), "an unknown skip reason");
    m = msg(77, 0, 0, 0);
    CHECK(!lb_msg_valid(&m), "an unknown kind");
    m = msg(LB_MSG_GPUINFO, 0, 3, 0);
    CHECK(!lb_msg_valid(&m), "an unknown GPU kind");
    for (int i = 0; i < LB_COUNT; i++) {
        CHECK(lb_name(i) && lb_unit(i), "names for %d", i);
        for (int k = 0; k < i; k++) CHECK(strcmp(lb_name(i), lb_name(k)), "unique %s", lb_name(i));
    }
    CHECK(!lb_name(LB_COUNT) && !lb_name(-1), "names out of range");

    /* ---- workloads, short ---- */
    double v;
    CHECK((v = lb_sha256(1, 0.1)) > 1, "sha256 %f", v);
    CHECK((v = lb_sha256(2, 0.1)) > 1, "sha256 x2 %f", v);
    CHECK((v = lb_matmul(1, 0.1)) > 0.01, "matmul %f", v);
    CHECK((v = lb_xz(1, 0.1)) > 1, "xz %f", v);
    CHECK((v = lb_js(1, 0.2)) > 0.5, "js %f", v);
    CHECK((v = lb_js(2, 0.2)) > 0.5, "js x2 %f", v);
    CHECK((v = lb_mem_copy(2, 8, 0.1)) > 0.05, "copy %f", v);
    CHECK((v = lb_mem_triad(2, 8, 0.1)) > 0.05, "triad %f", v);
    CHECK((v = lb_mem_latency(8, 200000)) > 0.1 && v < 10000, "latency %f", v);
    char tmpl[] = "/tmp/lordbench-disk-XXXXXX";
    int fd = mkstemp(tmpl);
    unlink(tmpl);
    double w, r, iops;
    CHECK(fd >= 0 && lb_disk(fd, 16, 0.2, &w, &r, &iops) == 0 && w > 0 && r > 0 && iops > 0, "disk %f %f %f", w, r, iops);
    struct stat st;
    CHECK(fd >= 0 && !fstat(fd, &st) && st.st_size == 0, "the scratch file is emptied");
    if (fd >= 0) close(fd);
    double a[] = { 5, 1, 3 }, sp;
    CHECK(lb_median(a, 3, &sp) == 3 && sp == 4, "median");
    double b2[] = { 4, 2 };
    CHECK(lb_median(b2, 2, &sp) == 3, "median of two");

    /* ---- conditions on a fake tree ---- */
    snprintf(root, sizeof root, "/tmp/lordbench-root-XXXXXX");
    if (!mkdtemp(root)) return 1;
    put("/sys/class/thermal/thermal_zone0/type", "x86_pkg_temp\n");
    put("/sys/class/thermal/thermal_zone0/temp", "61000\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_0_type", "passive\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_0_temp", "95000\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_1_type", "critical\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_1_temp", "105000\n");
    put("/sys/class/thermal/thermal_zone1/type", "acpitz\n");
    put("/sys/class/thermal/thermal_zone1/temp", "40000\n");
    put("/sys/class/thermal/thermal_zone1/trip_point_0_type", "critical\n");
    put("/sys/class/thermal/thermal_zone1/trip_point_0_temp", "0\n");          /* a placeholder: ignored */
    put("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "2400000\n");
    put("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "4200000\n");
    put("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "powersave\n");
    put("/sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count", "3\n");
    put("/sys/devices/system/cpu/cpu0/thermal_throttle/package_throttle_count", "1\n");
    put("/sys/class/power_supply/BAT0/type", "Battery\n");
    put("/sys/class/power_supply/BAT0/status", "Discharging\n");
    put("/sys/class/power_supply/BAT0/capacity", "25\n");
    put("/sys/class/power_supply/AC/type", "Mains\n");
    put("/sys/class/power_supply/AC/online", "0\n");
    put("/sys/class/power_supply/hidpp_battery_0/type", "Battery\n");
    put("/sys/class/power_supply/hidpp_battery_0/scope", "Device\n");
    put("/sys/class/power_supply/hidpp_battery_0/capacity", "90\n");
    put("/proc/stat", "cpu  100 0 50 800 50 0 0 0 0 0\ncpu0 1 2 3\n");
    LbCond c0, c1;
    lb_cond_read(root, &c0);
    CHECK(c0.temp_c == 61 && !strcmp(c0.zone, "x86_pkg_temp"), "hottest zone %f %s", c0.temp_c, c0.zone);
    CHECK(c0.passive_c == 95 && c0.critical_c == 105, "trips %f %f", c0.passive_c, c0.critical_c);
    CHECK(c0.throttle_known && c0.throttle_events == 4, "throttle counters %lu", c0.throttle_events);
    CHECK(c0.freq_mhz == 2400 && c0.freq_max_mhz == 4200 && !strcmp(c0.governor, "powersave"), "cpufreq");
    CHECK(c0.on_battery == 1 && c0.battery_pct == 25, "battery %d %d (a mouse's battery ignored)", c0.on_battery, c0.battery_pct);
    CHECK(lb_cond_battery_low(&c0), "below 30 %% on battery: refused");
    CHECK(c0.rpi_throttled == -1, "no Pi firmware");
    CHECK(!lb_cond_stop(&c0, &c0, 1), "nothing to stop at 61 °C");
    put("/sys/class/thermal/thermal_zone0/temp", "93500\n");
    lb_cond_read(root, &c1);
    CHECK(lb_cond_stop(&c0, &c1, 0) && !strcmp(lb_cond_stop(&c0, &c1, 0), "thermal"), "within 2 °C of passive: thermal");
    put("/sys/class/thermal/thermal_zone0/temp", "70000\n");
    put("/sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count", "5\n");
    lb_cond_read(root, &c1);
    CHECK(lb_cond_stop(&c0, &c1, 1) && !strcmp(lb_cond_stop(&c0, &c1, 1), "throttled"), "throttle counters grew in a peak test");
    CHECK(!lb_cond_stop(&c0, &c1, 0), "…recorded but not stopped in the sustained test");
    put("/sys/class/power_supply/AC/online", "1\n");
    put("/sys/class/power_supply/BAT0/status", "Charging\n");
    lb_cond_read(root, &c1);
    CHECK(c1.on_battery == 0 && !lb_cond_battery_low(&c1), "on mains");
    unsigned long long busy, total;
    CHECK(!lb_cpu_ticks(root, &busy, &total) && total == 1000 && busy == 150, "/proc/stat %llu/%llu", busy, total);

    /* a Raspberry Pi 5: no passive trip, the firmware's flags, hwmon's under-voltage alarm */
    snprintf(root, sizeof root, "/tmp/lordbench-pi-XXXXXX");
    if (!mkdtemp(root)) return 1;
    put("/sys/class/thermal/thermal_zone0/type", "cpu-thermal\n");
    put("/sys/class/thermal/thermal_zone0/temp", "55000\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_0_type", "critical\n");
    put("/sys/class/thermal/thermal_zone0/trip_point_0_temp", "110000\n");
    put("/sys/devices/platform/soc/soc:firmware/get_throttled", "0\n");
    lb_cond_read(root, &c0);
    CHECK(c0.rpi_throttled == 0 && c0.passive_c == 0 && c0.critical_c == 110, "Pi: flags %d trips %f %f", c0.rpi_throttled, c0.passive_c, c0.critical_c);
    CHECK(c0.on_battery == -1 && c0.battery_pct == -1, "Pi: no power_supply");
    put("/sys/class/thermal/thermal_zone0/temp", "101000\n");
    lb_cond_read(root, &c1);
    CHECK(lb_cond_stop(&c0, &c1, 0) && !strcmp(lb_cond_stop(&c0, &c1, 0), "thermal"), "Pi: within 10 °C of critical");
    put("/sys/class/thermal/thermal_zone0/temp", "80000\n");
    put("/sys/devices/platform/soc/soc:firmware/get_throttled", "50005\n");
    lb_cond_read(root, &c1);
    CHECK(c1.rpi_throttled == 0x50005, "Pi flags %#x", c1.rpi_throttled);
    CHECK(lb_cond_stop(&c0, &c1, 1) && !strcmp(lb_cond_stop(&c0, &c1, 1), "throttled"), "Pi: throttled now");
    put("/sys/devices/platform/soc/soc:firmware/get_throttled", "50000\n");     /* only "since boot" bits */
    put("/sys/class/hwmon/hwmon0/name", "rpi_volt\n");
    put("/sys/class/hwmon/hwmon0/in0_lcrit_alarm", "1\n");
    lb_cond_read(root, &c1);
    CHECK(!lb_cond_stop(&c0, &c1, 1) && (c1.rpi_throttled & LB_RPI_UV), "Pi: past events and under-voltage recorded, not a stop (%#x)", c1.rpi_throttled);

    /* ---- boot history ---- */
    char bt[] = "/tmp/lordbench-boots-XXXXXX";
    fd = mkstemp(bt);
    const char *lines =
        "{\"bootId\":\"a\",\"at\":1,\"seconds\":30.000,\"shown\":\"desktop\",\"version\":\"0.8.4-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"b\",\"at\":2,\"seconds\":20.000,\"shown\":\"desktop\",\"version\":\"0.8.4-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"c\",\"at\":3,\"seconds\":22.000,\"shown\":\"desktop\",\"version\":\"0.8.4-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"d\",\"at\":4,\"seconds\":40.000,\"shown\":\"greeter\",\"version\":\"0.8.5-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"e\",\"at\":5,\"seconds\":18.000,\"shown\":\"greeter\",\"version\":\"0.8.5-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"f\",\"at\":6,\"seconds\":21.000,\"shown\":\"greeter\",\"version\":\"0.8.5-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"g\",\"at\":7,\"seconds\":19.000,\"shown\":\"greeter\",\"version\":\"0.8.5-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"h\",\"at\":8,\"seconds\":\"x\",\"version\":\"0.8.5\\\"evil\"}\n";
    CHECK(fd >= 0 && write(fd, lines, strlen(lines)) == (ssize_t)strlen(lines), "boot file");
    fchmod(fd, 0644);
    close(fd);
    char *h = lbrun_boot_history(bt);
    CHECK(h && strstr(h, "{\"version\":\"0.8.4-beta\",\"boots\":2,\"median\":21.00,\"min\":20.00,\"max\":22.00,\"first\":30.00}"), "0.8.4: %s", h ? h : "null");
    CHECK(h && strstr(h, "{\"version\":\"0.8.5-beta\",\"boots\":3,\"median\":19.00,\"min\":18.00,\"max\":21.00,\"first\":40.00}"), "0.8.5: %s", h ? h : "null");
    CHECK(h && !strstr(h, "evil"), "a bad line is skipped");
    free(h);
    chmod(bt, 0666);
    CHECK(!(h = lbrun_boot_history(bt)), "a file others can write is refused");
    free(h);
    unlink(bt);

    /* ---- the run's state without a helper ---- */
    char *p = lbrun_poll();
    CHECK(p && strstr(p, "\"state\":\"idle\""), "idle: %s", p ? p : "null");
    free(p);
    CHECK(lbrun_start("turbo", 0, NULL) == -1 && lbrun_start("full", 5, NULL) == -1, "bad arguments refused");

    printf("lordbench-test: %d ok, %d failed\n", oks, fails);
    return fails != 0;
}
