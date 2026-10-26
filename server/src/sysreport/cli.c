/* cli.c — `lordos-sysreport`, System Information's command (see docs/BENCHMARK.md). An ordinary
 * user command: lord-server never runs it. It prints the same report as the app, so one can be made
 * on a machine without a session (a headless Pi, a VM over SSH, CI).
 *
 *   lordos-sysreport report [--json | --markdown]
 *
 * --root <dir> reads a fake tree instead of this machine (tests and fixtures only). */
#include "sysreport.h"
#include "lord_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
    fprintf(stderr, "usage: lordos-sysreport report [--json | --markdown]\n"
                    "  Prints this computer's hardware and what the system made of it (no serial numbers,\n"
                    "  addresses, host or user names). Markdown by default.\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *root = "", *mode = "markdown";
    int report = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "report")) report = 1;
        else if (!strcmp(argv[i], "--json")) mode = "json";
        else if (!strcmp(argv[i], "--markdown")) mode = "markdown";
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else return usage();
    }
    if (!report) return usage();

    SysReport *r = malloc(sizeof *r);
    if (!r) return 1;
    sysreport_collect(r, root, NULL, NULL);
    snprintf(r->sw.lordos, sizeof r->sw.lordos, "%s%s%s", LORD_VERSION, LORD_CHANNEL[0] ? "-" : "", LORD_CHANNEL);
    char *out = !strcmp(mode, "json") ? sysreport_json(r) : sysreport_markdown(r);
    free(r);
    if (!out) { fprintf(stderr, "lordos-sysreport: out of memory\n"); return 1; }
    fputs(out, stdout);
    free(out);
    return ferror(stdout) ? 1 : 0;
}
