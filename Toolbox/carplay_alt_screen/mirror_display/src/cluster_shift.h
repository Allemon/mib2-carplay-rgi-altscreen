/*
 * cluster_shift.h - user offset of the map plane (MMI-Cockpit-Carplay fork).
 *
 * GEM "Cluster map shift" (Toolbox/scripts/cluster_shift.sh) writes
 * /mnt/app/root/carplay-altscreen/state/cluster-shift.txt:
 *
 *     full_dx=<px>
 *     small_dx=<px>
 *
 * The renderer adds the value for the current view on top of the OEM stage
 * offset (Sport SMALL is already -476 from the stock layout). "Sport test"
 * writes small_dx=-476, so a Classic car shows the SMALL view where Sport puts
 * it. Values outside +-720 and unknown lines count as 0; no file means no
 * shift. The file is small and read on the renderer's 50 ms placement poll, so
 * a change applies while CarPlay is connected.
 */
#ifndef CLUSTER_SHIFT_H
#define CLUSTER_SHIFT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int kClusterShiftMaxDx = 720;

struct ClusterShift {
    int full_dx;
    int small_dx;
};

static inline int cluster_shift_value(const char *line, const char *key) {
    const size_t n = strlen(key);
    char *end = 0;
    long v;
    if (strncmp(line, key, n) != 0 || line[n] != '=') return 0;
    v = strtol(line + n + 1u, &end, 10);
    if (end == line + n + 1u) return 0;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end || v < -kClusterShiftMaxDx || v > kClusterShiftMaxDx) return 0;
    return (int)v;
}

/* Parse one line into s; lines for other keys leave s unchanged. */
static inline void cluster_shift_parse_line(const char *line, ClusterShift *s) {
    if (!strncmp(line, "full_dx=", 8u)) s->full_dx = cluster_shift_value(line, "full_dx");
    else if (!strncmp(line, "small_dx=", 9u)) s->small_dx = cluster_shift_value(line, "small_dx");
}

static inline ClusterShift cluster_shift_load(const char *path) {
    ClusterShift s;
    char line[64];
    s.full_dx = 0;
    s.small_dx = 0;
    FILE *f = path ? fopen(path, "r") : 0;
    if (!f) return s;
    int lines = 0;
    while (lines++ < 8 && fgets(line, sizeof(line), f))
        cluster_shift_parse_line(line, &s);
    fclose(f);
    return s;
}

#endif
