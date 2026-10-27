/* lbcond.h — what LordBench records about the machine around a run, and its thermal and power guards
 * (docs/BENCHMARK.md, "Thermal and power safety", "Recorded conditions"). World-readable sysfs and
 * procfs only, read by lord-server (never by lord-bench). Every reader takes a root folder ("" for the
 * real one) so tests can feed fake trees. */
#ifndef LORD_BENCH_LBCOND_H
#define LORD_BENCH_LBCOND_H

#include <stdint.h>

typedef struct {
    double temp_c;                  /* the hottest thermal zone or CPU hwmon sensor, °C; < -270 unknown */
    double passive_c, critical_c;   /* the lowest passive and critical trip points, °C; 0 unknown */
    char   zone[24];                /* the hottest zone's type ("x86_pkg_temp", "cpu-thermal", …) */
    unsigned long throttle_events;  /* x86 core + package thermal throttle counters, summed */
    int    throttle_known;          /* 1 if those counters exist */
    int    rpi_throttled;           /* the Pi firmware's get_throttled bits (-1: no such file) */
    double freq_mhz, freq_max_mhz;  /* the average current and the highest maximum cpufreq, 0 unknown */
    char   governor[24];
    char   platform_profile[24];    /* /sys/firmware/acpi/platform_profile, "" without one */
    int    on_battery;              /* 1 battery, 0 mains, -1 unknown (no power_supply: a desktop or VM) */
    int    battery_pct;             /* -1 without a battery */
} LbCond;

void lb_cond_read(const char *root, LbCond *c);

/* the Pi firmware's flags: 0 under-voltage now, 1 frequency capped now, 2 throttled now, 3 soft
 * temperature limit now; bits 16-19 the same since boot */
enum { LB_RPI_UV = 1, LB_RPI_CAPPED = 2, LB_RPI_THROTTLED = 4, LB_RPI_SOFTTEMP = 8 };

/* CPU busy share between two reads of /proc/stat: lb_cpu_ticks fills busy and total */
int    lb_cpu_ticks(const char *root, unsigned long long *busy, unsigned long long *total);

/* the kind of device holding the folder `path` is on: "nvme", "sd" (an SD card or eMMC through
 * mmcblk), "ssd", "hdd", "usb", "virtual", "ram" (tmpfs, overlay of the live session) or "unknown" */
const char *lb_storage_kind(const char *root, const char *path);

/* why a run must stop now, or NULL: "thermal" when the temperature is within 2 °C of the passive trip
 * point (or within 10 °C of the critical one when there's no passive), "throttled" when the throttle
 * counters grew or the Pi says it throttles since `start` (only checked when peak = 1) */
const char *lb_cond_stop(const LbCond *start, const LbCond *now, int peak);

/* refused before starting: on battery below 30 % */
int lb_cond_battery_low(const LbCond *c);

#endif
