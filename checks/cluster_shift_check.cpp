// Host check of mirror_display/src/cluster_shift.h (run by scripts/run_checks.sh).
#include "cluster_shift.h"

#include <stdio.h>
#include <unistd.h>

static int failures = 0;

static void expect(const char *what, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", what, got, want);
        ++failures;
    }
}

static ClusterShift from_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
    return cluster_shift_load(path);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "cluster-shift.txt";
    ClusterShift s;

    unlink(path);
    s = cluster_shift_load(path);
    expect("no file full", s.full_dx, 0);
    expect("no file small", s.small_dx, 0);

    s = from_text(path, "small_dx=-476\n");
    expect("sporttest full", s.full_dx, 0);
    expect("sporttest small", s.small_dx, -476);

    s = from_text(path, "full_dx=12\r\nsmall_dx=-20\r\n");
    expect("crlf full", s.full_dx, 12);
    expect("crlf small", s.small_dx, -20);

    s = from_text(path, "full_dx=721\nsmall_dx=-720\n");
    expect("full out of range", s.full_dx, 0);
    expect("small at limit", s.small_dx, -720);

    s = from_text(path, "small_dx=-47x\nfull_dx=\nfull_dxx=5\n");
    expect("garbage small", s.small_dx, 0);
    expect("empty full", s.full_dx, 0);

    s = from_text(path, "small_dx_extra=9\n");
    expect("other key", s.small_dx, 0);

    unlink(path);
    if (failures) return 1;
    printf("CLUSTER_SHIFT_CHECK=PASS\n");
    return 0;
}
