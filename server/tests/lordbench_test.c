/* lordbench_test.c — LordBench without the helper process (docs/BENCHMARK.md, "0.8.6 — LordBench"): the
 * records lord-server accepts from lord-bench and from lord-media's video measurement (and the helper-sandbox
 * self-test's), the compositor's frame statistics, short runs of every CPU, memory and storage workload (the
 * UI engine's scene and the shaper's paragraphs with every glyph in the built-in fonts), the
 * conditions read from fake sysfs trees (temperatures and trip points, throttle counters, the Pi's
 * flags, batteries) with the thermal and battery guards, the boot history per version and the boot time
 * test's state file and its steps. */
#include "../src/bench/lbproto.h"
#include "../src/bench/lbwork.h"
#include "../src/bench/lbui.h"
#include "../src/bench/lbtext.h"
#include "../src/bench/lbcond.h"
#include "../src/bench/lbrun.h"
#include "../src/bench/lbframes.h"
#include "../src/bench/lbinput.h"
#include "../src/bench/lbvideo.h"
#include "../src/bench/boottest.h"
#include "../src/media/proto.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
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
    /* lord-bench may not report what lord-media or lord-server measure */
    m = msg(LB_MSG_RESULT, LB_VID_H264_1080, 60, 1);
    CHECK(!lb_msg_valid(&m), "a video result from lord-bench");
    m = msg(LB_MSG_RESULT, LB_COMP_P99, 3, 0);
    CHECK(!lb_msg_valid(&m), "a frame time from lord-bench");
    m = msg(LB_MSG_STEP, LB_DESK_FPS, 0, 0);
    CHECK(!lb_msg_valid(&m), "a desktop step from lord-bench");
    m = msg(LB_MSG_SKIP, LB_SUS_END, 0, LB_SKIP_NOCODEC);
    CHECK(!lb_msg_valid(&m), "a video skip reason from lord-bench");
    CHECK(lb_lower_better(LB_COMP_P99) && lb_lower_better(LB_DESK_MISSED) && !lb_lower_better(LB_DESK_FPS) && !lb_lower_better(LB_VID_AV1_1080), "which are lower-better");
    CHECK(!strcmp(lb_name(LB_VENC_VP8), "video.enc.vp8") && !strcmp(lb_unit(LB_COMP_MISSED), "%"), "video and frame names");
    CHECK(!strcmp(lb_name(LB_CPU_UIN), "cpu.ui.n") && !strcmp(lb_unit(LB_CPU_UI1), "fps") && !strcmp(lb_name(LB_CPU_SHAPE1), "cpu.shape.1"), "UI names");
    m = msg(LB_MSG_RESULT, LB_CPU_SHAPEN, 1200, 3);
    CHECK(lb_msg_valid(&m), "a shaping result");
    /* the helper-sandbox self-test's records */
    m = msg(LB_MSG_SANDBOX, LB_SBX_EXEC, 1, 1);
    CHECK(lb_msg_valid(&m), "a refused try");
    m = msg(LB_MSG_SANDBOX, LB_SBX_READ, 0, 0);
    CHECK(lb_msg_valid(&m), "a try that worked");
    m = msg(LB_MSG_SANDBOX, LB_SBX_COUNT, 1, 1);
    CHECK(!lb_msg_valid(&m), "a try out of range");
    m = msg(LB_MSG_SANDBOX, LB_SBX_FORK, 0.5, 1);
    CHECK(!lb_msg_valid(&m), "a try neither refused nor not");
    m = msg(LB_MSG_SANDBOX, LB_SBX_FORK, 1, 4096);
    CHECK(!lb_msg_valid(&m), "an errno out of range");
    m = msg(LB_MSG_SANDBOX, LB_SBX_FORK, 1, 1.5);
    CHECK(!lb_msg_valid(&m), "a fractional errno");
    for (int i = 0; i < LB_SBX_COUNT; i++) CHECK(lb_sbx_name(i) && !strncmp(lb_sbx_name(i), "helper", 6), "try names %d", i);
    CHECK(!lb_sbx_name(LB_SBX_COUNT) && !lb_sbx_name(-1) && !strcmp(lb_sbx_name(LB_SBX_FORK), "helperFork"), "try names out of range");

    /* ---- lord-media's video records ---- */
    CHECK(lbv_record_valid(VB_HEVC_2160, VBR_RESULT, 24500, 300, 0), "a video result");
    CHECK(lbv_record_valid(VB_AV1_1080, VBR_SKIP, 0, 0, VBS_NOCODEC), "a video skip");
    CHECK(lbv_record_valid(VB_ENC_VP8, VBR_STEP, 0, 0, 0), "a video step");
    CHECK(!lbv_record_valid(VB_COUNT, VBR_RESULT, 1000, 0, 0), "a video id out of range");
    CHECK(!lbv_record_valid(VB_H264_1080, VBR_RESULT, 0, 0, 0), "a zero rate");
    CHECK(!lbv_record_valid(VB_H264_1080, VBR_RESULT, -5, 0, 0), "a negative rate");
    CHECK(!lbv_record_valid(VB_H264_1080, VBR_RESULT, 1000, -1, 0), "a negative spread");
    CHECK(!lbv_record_valid(VB_H264_1080, VBR_RESULT, 200000000LL, 0, 0), "an absurd rate");
    CHECK(!lbv_record_valid(VB_H264_1080, VBR_SKIP, 0, 0, 9), "an unknown video skip reason");
    CHECK(!lbv_record_valid(VB_H264_1080, 7, 0, 0, 0), "an unknown video record kind");

    /* ---- the compositor's frame recorder ---- */
    LbFrameStats fs;
    lbf_frame(5);                                       /* not recording: ignored */
    lbf_presented(1000, 16666667);
    lbf_begin();
    uint64_t t = 1000000000ull;
    for (int i = 0; i < 100; i++) {
        lbf_frame(i == 99 ? 40.0 : 2.0 + (i % 3));      /* one slow frame */
        t += i == 50 || i == 70 ? 50000000ull : 16666667ull;   /* two long waits */
        lbf_presented(t, 16666667);
    }
    CHECK(lbf_end(&fs) == 0, "frame stats");
    CHECK(fs.frames == 100 && fs.shown == 100, "frames %d shown %d", fs.frames, fs.shown);
    CHECK(fabs(fs.budget_ms - 16.667) < 0.01, "budget %f", fs.budget_ms);
    CHECK(fs.work_p50 == 3.0, "p50 %f", fs.work_p50);
    CHECK(fs.work_p99 == 4.0 && fs.work_max == 40.0, "p99 %f max %f", fs.work_p99, fs.work_max);
    CHECK(fabs(fs.missed_pct - 200.0 / 99) < 0.01, "missed %f", fs.missed_pct);
    CHECK(fabs(fs.gap_max - 50) < 0.01, "gap %f", fs.gap_max);
    CHECK(fs.fps > 50 && fs.fps < 60, "fps %f", fs.fps);
    lbf_frame(9);                                       /* stopped: ignored */
    lbf_begin();
    lbf_presented(5, 0);
    CHECK(lbf_end(&fs) == -1 && fs.shown == 1 && fs.frames == 0, "one frame is not enough");
    lbf_begin();
    lbf_presented(100, 0);
    lbf_presented(50, 0);                               /* out of order: dropped */
    lbf_presented(200, 0);
    CHECK(lbf_end(&fs) == 0 && fs.shown == 2, "out-of-order times dropped");

    /* ---- input latency: events applied before a frame end at that frame's presentation ---- */
    lbf_input(1000);                                    /* not recording: ignored */
    lbf_begin();
    lbf_presented(10000000, 16666667);
    lbf_input(11000000);                                /* two events, then a frame the main display gets */
    lbf_input(19000000);
    lbf_built(1);
    lbf_input(20000000);                                /* applied after that frame was built: the next one */
    lbf_presented(26666667, 16666667);                  /* 15.67 and 7.67 ms */
    lbf_built(0);                                       /* a frame only another display got: still waiting */
    lbf_presented(43333334, 16666667);                  /* nothing in it */
    lbf_built(1);
    lbf_presented(60000000, 16666667);                  /* 40 ms */
    CHECK(lbf_end(&fs) == 0 && fs.inputs == 3, "input events %d", fs.inputs);
    CHECK(fabs(fs.in_p50 - 15.666667) < 0.001 && fabs(fs.in_p99 - 40) < 0.001, "input p50 %f p99 %f", fs.in_p50, fs.in_p99);
    lbf_begin();
    for (int i = 0; i < 200; i++) lbf_input(1000 + (uint64_t)i);   /* more than one frame can hold: the newest kept */
    lbf_built(1);
    lbf_presented(5000, 0);
    lbf_presented(6000, 0);
    CHECK(lbf_end(&fs) == 0 && fs.inputs == 64 && fabs(fs.in_p99 - (5000 - 1136) / 1e6) < 1e-9, "a full batch keeps the newest %d %f", fs.inputs, fs.in_p99);
    lbf_begin();
    lbf_input(9000);
    lbf_built(1);
    lbf_presented(5000, 0);                             /* a presentation stamped before the event: not a latency */
    lbf_presented(6000, 0);
    CHECK(lbf_end(&fs) == 0 && fs.inputs == 0, "no negative latency");

    /* ---- the synthetic pointer: its path, and the thread's events through the eventfd ---- */
    float dx, dy;
    lbi_path(200, 100, 4000, 1000, &dx, &dy);           /* a quarter turn: x = rx, y = ry sin(π) / 2 = 0 */
    CHECK(fabs(dx - 200) < 0.01 && fabs(dy) < 0.01, "path at a quarter %f %f", dx, dy);
    lbi_path(200, 100, 4000, 500, &dx, &dy);            /* an eighth: x = rx sin(π/4), y = ry / 2 */
    CHECK(fabs(dx - 141.42) < 0.01 && fabs(dy - 50) < 0.01, "path at an eighth %f %f", dx, dy);
    CHECK(lbi_start(10, 10, 100) == -1 && lbi_fd() == -1, "a period under 500 ms is refused");
    CHECK(lbi_take(&dx, &dy) == 0, "nothing to take while stopped");
    lbf_begin();
    CHECK(lbi_start(200, 100, 2000) == 0 && lbi_fd() >= 0, "the synthetic pointer starts");
    int took = 0, wakes = 0;
    for (int i = 0; i < 12; i++) {                      /* about 12 frames of 16 ms */
        struct pollfd pf = { lbi_fd(), POLLIN, 0 };
        if (poll(&pf, 1, 100) == 1) { wakes++; lbi_drain(); }
        struct timespec ts = { 0, 16000000 };
        nanosleep(&ts, NULL);
        if (lbi_take(&dx, &dy)) took++;
        lbf_built(1);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        lbf_presented((uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec, 16666667);
    }
    lbi_stop();
    CHECK(lbi_fd() == -1 && lbi_take(&dx, &dy) == 0, "the synthetic pointer stops");
    CHECK(wakes >= 10 && took >= 10, "it wakes the loop like a device (%d wakes, %d frames with events)", wakes, took);
    CHECK(lbf_end(&fs) == 0 && fs.inputs >= 15 && fs.inputs <= 30, "about 125 events a second: %d in ~0.2 s", fs.inputs);
    CHECK(fs.in_p50 > 0 && fs.in_p99 < 100, "latencies %f %f", fs.in_p50, fs.in_p99);
    CHECK(fabs(dx) <= 200.01 && fabs(dy) <= 50.01, "positions on the path %f %f", dx, dy);

    /* ---- workloads, short ---- */
    double v;
    CHECK((v = lb_sha256(1, 0.1)) > 1, "sha256 %f", v);
    CHECK((v = lb_sha256(2, 0.1)) > 1, "sha256 x2 %f", v);
    CHECK((v = lb_matmul(1, 0.1)) > 0.01, "matmul %f", v);
    CHECK((v = lb_xz(1, 0.1)) > 1, "xz %f", v);
    CHECK((v = lb_js(1, 0.2)) > 0.5, "js %f", v);
    CHECK((v = lb_js(2, 0.2)) > 0.5, "js x2 %f", v);
    /* the UI engine's scene: every style key known, then frames on one thread and with the paint bands */
    int nodes = 0, unknown = -1;
    CHECK(lb_ui_check(&nodes, &unknown) == 0 && nodes > 400 && unknown == 0, "scene: %d nodes, %d unknown styles", nodes, unknown);
    CHECK((v = lb_ui(1, 0.3)) > 0.5, "ui %f", v);
    CHECK((v = lb_ui(4, 0.3)) > 0.5, "ui x4 %f", v);
    /* the shaper: each paragraph shapes to real glyphs of its built-in fonts (none missing: the subsets cover
     * them), the same every time */
    for (int p = 0; p < LB_PARA_COUNT; p++) {
        int g = 0, bad = -1, g2 = 0, bad2 = -1;
        long adv = 0, adv2 = 0;
        CHECK(lb_shape_check(p, &g, &bad, &adv) == 0 && g > 80 && bad == 0 && adv > 0, "paragraph %d: %d glyphs, %d missing, advance %ld", p, g, bad, adv);
        CHECK(lb_shape_check(p, &g2, &bad2, &adv2) == 0 && g2 == g && bad2 == bad && adv2 == adv, "paragraph %d shapes the same twice", p);
    }
    CHECK(lb_shape_check(LB_PARA_COUNT, NULL, NULL, NULL) == -1, "a paragraph out of range");
    CHECK((v = lb_shape(1, 0.2)) > 1, "shape %f", v);
    CHECK((v = lb_shape(2, 0.2)) > 1, "shape x2 %f", v);
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
        "{\"bootId\":\"h\",\"at\":8,\"seconds\":\"x\",\"version\":\"0.8.5\\\"evil\"}\n"
        "{\"bootId\":\"i\",\"at\":9,\"seconds\":50.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\"}\n"
        "{\"bootId\":\"j\",\"at\":10,\"seconds\":31.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\",\"test\":1,\"testOf\":3}\n"
        "{\"bootId\":\"k\",\"at\":11,\"seconds\":30.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\",\"test\":2,\"testOf\":3}\n"
        "{\"bootId\":\"l\",\"at\":12,\"seconds\":29.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\",\"test\":1,\"testOf\":4}\n"
        "{\"bootId\":\"m\",\"at\":13,\"seconds\":33.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\",\"test\":2,\"testOf\":4}\n"
        "{\"bootId\":\"n\",\"at\":14,\"seconds\":99.000,\"shown\":\"login\",\"version\":\"0.8.6-beta\",\"build\":\"x\",\"kernel\":\"k\",\"test\":4,\"testOf\":4}\n";
    CHECK(fd >= 0 && write(fd, lines, strlen(lines)) == (ssize_t)strlen(lines), "boot file");
    fchmod(fd, 0644);
    close(fd);
    char *h = lbrun_boot_history(bt);
    CHECK(h && strstr(h, "{\"version\":\"0.8.4-beta\",\"boots\":2,\"median\":21.00,\"min\":20.00,\"max\":22.00,\"first\":30.00}"), "0.8.4: %s", h ? h : "null");
    CHECK(h && strstr(h, "{\"version\":\"0.8.5-beta\",\"boots\":3,\"median\":19.00,\"min\":18.00,\"max\":21.00,\"first\":40.00}"), "0.8.5: %s", h ? h : "null");
    CHECK(h && !strstr(h, "evil"), "a bad line is skipped");
    CHECK(h && strstr(h, "\"test\":{\"boots\":2,\"of\":4,\"at\":12,\"median\":31.00,\"min\":29.00,\"max\":33.00}"),
          "the latest boot time test, a boot out of order left out: %s", h ? h : "null");
    CHECK(h && !strstr(h, "0.8.5-beta\",\"boots\":3,\"median\":19.00,\"min\":18.00,\"max\":21.00,\"first\":40.00,\"test\""), "no test in 0.8.5");
    free(h);
    chmod(bt, 0666);
    CHECK(!(h = lbrun_boot_history(bt)), "a file others can write is refused");
    free(h);
    unlink(bt);

    /* ---- the boot time test's state (bench/boottest.c) ---- */
    BootTest b = { 5, 5, 1793000000LL }, bs, after;
    char text[128];
    CHECK(bt_format(&b, text, sizeof text) == 0 && !strcmp(text, "lordos-boot-test 1\ntotal 5\nleft 5\nat 1793000000\n"), "format: %s", text);
    CHECK(bt_parse(text, &bs) == 0 && bs.total == 5 && bs.left == 5 && bs.at == 1793000000LL, "parse");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 11\nleft 5\nat 1\n", &bs), "more than BT_MAX refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 1\nleft 1\nat 1\n", &bs), "fewer than BT_MIN refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 3\nleft 4\nat 1\n", &bs), "left above total refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 3\nleft 0\nat 1\n", &bs), "nothing left refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 3\nleft 2\nat 1\nrm -rf /\n", &bs), "trailing text refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 3\nleft 2\nat -1\n", &bs), "a negative time refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal  3\nleft 2\nat 1\n", &bs), "extra spaces refused");
    CHECK(bt_parse("lordos-boot-test 2\ntotal 3\nleft 2\nat 1\n", &bs), "another format version refused");
    CHECK(bt_parse("lordos-boot-test 1\ntotal 3\nleft 2\nat 99999999999999\n", &bs), "a huge number refused");
    BootTest bad = { 3, 0, 1 };
    CHECK(bt_format(&bad, text, sizeof text) == -1, "an invalid state isn't written");
    int k = 0;
    long long now = 1793000100LL;
    CHECK(bt_next(0, &b, now, &k, &after) == BT_NONE, "no file, no test");
    CHECK(bt_next(1, &b, now, &k, &after) == BT_AGAIN && k == 1 && after.left == 4 && after.total == 5 && after.at == now, "boot 1 of 5");
    BootTest last = { 5, 1, now - 100 };
    CHECK(bt_next(1, &last, now, &k, &after) == BT_LAST && k == 5, "boot 5 of 5 is the last");
    BootTest old = { 5, 3, now - BT_STALE_S - 1 }, future = { 5, 3, now + 3600 };
    CHECK(bt_next(1, &old, now, &k, &after) == BT_STALE, "an old state is discarded");
    CHECK(bt_next(1, &future, now, &k, &after) == BT_STALE, "a state from the future is discarded");
    /* the test always ends: from any valid state, at most BT_MAX boots */
    BootTest cur = { BT_MAX, BT_MAX, now };
    int boots = 0, act;
    while ((act = bt_next(1, &cur, now, &k, &after)) == BT_AGAIN && boots < 100) { cur = after; boots++; }
    CHECK(act == BT_LAST && boots == BT_MAX - 1 && k == BT_MAX, "%d restarts for %d boots", boots, BT_MAX);
    char bf[] = "/tmp/lordbench-boottest-XXXXXX";
    fd = mkstemp(bf);
    close(fd);
    CHECK(bt_write(bf, &b) == 0, "write");
    CHECK(getuid() == 0 ? bt_read(bf, &bs) == 0 : bt_read(bf, &bs) == -1, "read only a root-owned file");
    unlink(bf);

    /* ---- the run's state without a helper ---- */
    char *p = lbrun_poll();
    CHECK(p && strstr(p, "\"state\":\"idle\""), "idle: %s", p ? p : "null");
    free(p);
    CHECK(lbrun_start("turbo", 0, NULL) == -1 && lbrun_start("full", 5, NULL) == -1, "bad arguments refused");

    printf("lordbench-test: %d ok, %d failed\n", oks, fails);
    return fails != 0;
}
