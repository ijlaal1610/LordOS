/* fwdest_test.c — the destinations of server/tests/fw_dest_cases.txt through the store's parser
 * (fw_parse_dest, then fw_format_dest): each one must be refused ("!") or come back as the canonical
 * text. The same file is checked against the Settings sheet's parser (sdk/fwrule.js) by
 * tests/fwrule_input_test.mjs, so what the sheet accepts is what lord.fw.addRule stores.
 *
 *   build/fwdest-test [cases file]      (default: tests/fw_dest_cases.txt, run from server/) */
#include "../src/fw/rules.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tests/fw_dest_cases.txt";
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    char line[600];
    int n = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *tab = strchr(line, '\t');
        if (!tab) { fprintf(stderr, "malformed line: %s\n", line); bad++; continue; }
        *tab = 0;
        const char *in = line, *want = tab + 1;
        FwRule r;
        memset(&r, 0, sizeof r);
        char got[FW_NAME_MAX + 8] = "!";
        if (!fw_parse_dest(in, &r) && fw_format_dest(&r, got, sizeof got)) snprintf(got, sizeof got, "(format failed)");
        n++;
        if (strcmp(got, want)) { fprintf(stderr, "✗ %-40s want %-24s got %s\n", in, want, got); bad++; }
    }
    fclose(f);
    printf("fwdest-test: %d cases, %d failed\n", n, bad);
    return bad != 0 || n == 0;
}
