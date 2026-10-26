/* cli.c — `lordos-sysreport` and `lordos-fetch`, System Information's commands (see
 * docs/BENCHMARK.md). Ordinary user commands: lord-server never runs them. The report is the same as
 * the app's, so one can be made on a machine without a session (a headless Pi, a VM over SSH, CI).
 *
 *   lordos-sysreport report [--json | --markdown]
 *   lordos-fetch [--share] [--color=auto|always|never]      (also: lordos-sysreport fetch …)
 *
 * lordos-fetch is a link to this binary. --root <dir> reads a fake tree instead of this machine
 * (tests and fixtures only). */
#include "sysreport.h"
#include "fetch.h"
#include "lord_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int usage(int fetch) {
    if (fetch)
        fprintf(stderr, "usage: lordos-fetch [--share] [--color=auto|always|never]\n"
                        "  Shows the crown and a summary of this computer. --share leaves out the user and\n"
                        "  host names, for a screenshot you will post.\n");
    else
        fprintf(stderr, "usage: lordos-sysreport report [--json | --markdown]\n"
                        "       lordos-sysreport fetch [--share] [--color=auto|always|never]\n"
                        "  report prints this computer's hardware and what the system made of it (no serial\n"
                        "  numbers, addresses, host or user names). Markdown by default.\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *root = "", *mode = "markdown", *color = "auto";
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    int fetch = !strcmp(base, "lordos-fetch"), report = 0, share = 0;
    for (int i = 1; i < argc; i++) {
        if (!fetch && !report && !strcmp(argv[i], "report")) report = 1;
        else if (!fetch && !report && !strcmp(argv[i], "fetch")) fetch = 1;
        else if (report && !strcmp(argv[i], "--json")) mode = "json";
        else if (report && !strcmp(argv[i], "--markdown")) mode = "markdown";
        else if (fetch && !strcmp(argv[i], "--share")) share = 1;
        else if (fetch && !strncmp(argv[i], "--color=", 8)
                 && (!strcmp(argv[i] + 8, "auto") || !strcmp(argv[i] + 8, "always") || !strcmp(argv[i] + 8, "never"))) color = argv[i] + 8;
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else return usage(fetch);
    }
    if (!report && !fetch) return usage(0);

    SysReport *r = malloc(sizeof *r);
    if (!r) return 1;
    sysreport_collect(r, root, NULL, NULL);
    snprintf(r->sw.lordos, sizeof r->sw.lordos, "%s%s%s", LORD_VERSION, LORD_CHANNEL[0] ? "-" : "", LORD_CHANNEL);
    char *out;
    if (fetch) {
        FetchLive l;
        fetch_live(&l, root, share);
        out = fetch_render(r, &l, fetch_color_mode(color, isatty(1)));
    } else {
        out = !strcmp(mode, "json") ? sysreport_json(r) : sysreport_markdown(r);
    }
    free(r);
    if (!out) { fprintf(stderr, "%s: out of memory\n", base); return 1; }
    fputs(out, stdout);
    free(out);
    return ferror(stdout) ? 1 : 0;
}
