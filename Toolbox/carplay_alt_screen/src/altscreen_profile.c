/* altscreen_profile.c - ThemeAssets declaration profiles and bounded TLV mutator. */
#include "altscreen_profile.h"
#include "altscreen_core.h"
#include "altscreen_paths.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

const struct altscreen_profile altscreen_prof_observe = {
    "observe", 0, 0, 0, "none - parse and log only"
};
/* MHI3/iOS gate: WirelessCarPlay transport component parameter 21,
 * TransportSupportsThemeAssets void subparameter 17. Parameter 20 is accepted
 * as an existing-parent compatibility target by the mutator below. */
const struct altscreen_profile altscreen_prof_theme_21_17 = {
    "theme_21_17", 1, 21, 17,
    "TransportSupportsThemeAssets (MHI3/iOS reverse-verified mapping)"
};

static const struct altscreen_profile *const kAll[] = {
    &altscreen_prof_observe,
    &altscreen_prof_theme_21_17,
    NULL
};

/* Safe process default remains observe. FULL_CHAIN selects theme_21_17 explicitly. */
static const struct altscreen_profile *g_current = &altscreen_prof_observe;

void altscreen_profile_set(const struct altscreen_profile *p) {
    g_current = p ? p : &altscreen_prof_observe;
}

const struct altscreen_profile *altscreen_profile_current(void) { return g_current; }

static void fold(char *s) {
    for (; *s; ++s) {
        if (*s == (char)45) *s = (char)95;
        else if (*s >= (char)65 && *s <= (char)90) *s = (char)(*s + 32);
    }
}

const struct altscreen_profile *altscreen_profile_by_name(const char *name) {
    char buf[ALTPROF_NAME_MAX];
    int i;
    if (!name || !*name) return &altscreen_prof_observe;
    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    fold(buf);
    for (i = 0; kAll[i]; ++i)
        if (!strcmp(buf, kAll[i]->name)) return kAll[i];
    if (!strcmp(buf, "off") || !strcmp(buf, "none") || !strcmp(buf, "log_only"))
        return &altscreen_prof_observe;
    if (!strcmp(buf, "21_17") || !strcmp(buf, "theme") ||
        !strcmp(buf, "theme_assets") || !strcmp(buf, "param21") ||
        !strcmp(buf, "legacy_21_17"))
        return &altscreen_prof_theme_21_17;
    /* usbhost_16_5 was an incorrect experimental interpretation: LIVI and
     * CatPlay identify nested subparameter 5 as transport iAP2 support, not
     * ThemeAssets. Old markers therefore fail safely to observe-only. */
    if (!strcmp(buf, "16_5") || !strcmp(buf, "usbhost") ||
        !strcmp(buf, "usbhost_16_5") || !strcmp(buf, "param16"))
        return &altscreen_prof_observe;
    return &altscreen_prof_observe;
}

const struct altscreen_profile *altscreen_profile_load(const char *path) {
    char buf[ALTPROF_NAME_MAX + 16];
    char own[ALTSCREEN_PATH_MAX];
    const struct altscreen_profile *p;
    const char *use;
    FILE *f;
    size_t n, i;

    use = path;
    if (!use && altscreen_state_path("IAP2_PROFILE", own, sizeof(own))) use = own;
    if (!use) {
        altscreen_profile_set(&altscreen_prof_observe);
        altscreen_log("%s", "PROFILE path unresolved -> observe");
        return g_current;
    }
    f = fopen(use, "rb");
    if (!f) {
        altscreen_profile_set(&altscreen_prof_observe);
        altscreen_log("PROFILE missing file=%s -> observe", use);
        return g_current;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    for (i = 0; i < n; ++i)
        if (buf[i] == (char)10 || buf[i] == (char)13 || buf[i] == (char)9 ||
            buf[i] == (char)32) buf[i] = 0;
    if (!buf[0]) {
        altscreen_profile_set(&altscreen_prof_observe);
        altscreen_log("PROFILE empty file=%s -> observe", use);
        return g_current;
    }
    p = altscreen_profile_by_name(buf);
    altscreen_profile_set(p);
    altscreen_log("PROFILE file=%s content=%s resolved=%s modifies=%d pair=%u/%u component=%s",
                  use, buf, p->name, p->modifies_bytes, p->param_id, p->sub_id, p->component);
    return p;
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

int altscreen_tlv_advertise(uint8_t *body, size_t *len, size_t capacity,
                            const struct altscreen_profile *p) {
    size_t off = 0;
    int invalid_theme_parent = 0;
    if (!body || !len || !p || *len > capacity) return -1;
    if (!p->modifies_bytes) return 0;

    while (off + 4 <= *len) {
        uint16_t tl = get16(body + off);
        uint16_t id = get16(body + off + 2);
        size_t end;
        int theme_target = p->param_id == 21u && p->sub_id == 17u &&
                           (id == 20u || id == 21u);
        if (tl < 4 || off + tl > *len) return -2;
        end = off + tl;
        if (theme_target || id == p->param_id) {
            size_t sub = off + 4;
            int has_identity_id = 0, has_identity_name = 0, has_flag = 0;
            while (sub + 4 <= end) {
                uint16_t sl = get16(body + sub);
                uint16_t sid = get16(body + sub + 2);
                if (sl < 4 || sub + sl > end) return -3;
                if (sid == 0u && sl == 6u) has_identity_id = 1;
                if (sid == 1u && sl >= 5u && sl <= 68u) has_identity_name = 1;
                if (sid == p->sub_id) has_flag = 1;
                sub += sl;
            }
            if (sub != end) return -3;
            /* ThemeAssets is legal only on a structurally identified transport
             * component. A coincidental parameter 20/21 must remain untouched. */
            if (!theme_target || (has_identity_id && has_identity_name)) {
                if (has_flag) return 0;
                if (*len + 4 > capacity) return -4;
                memmove(body + end + 4, body + end, *len - end);
                put16(body + end, 4);
                put16(body + end + 2, p->sub_id);
                put16(body + off, (uint16_t)(tl + 4));
                *len += 4;
                return 1;
            }
            if (theme_target) invalid_theme_parent = 1;
        }
        off += tl;
    }
    if (off != *len) return -5;
    if (invalid_theme_parent) {
        altscreen_log("ERROR IAP2 ThemeAssets parent20/21 lacks transport identity; refusing duplicate synthesis");
        return -7;
    }

    /* The modern ThemeAssets flag must not be synthesized as a parent that
     * contains only sub17. Clone the stock transport identity (sub0 + sub1)
     * from a legacy USBHost/Wireless transport component and add only the
     * reverse-verified void flag. This preserves the factory-generated
     * component identifier/name and never invents authentication identity. */
    if (p->param_id == 21u && p->sub_id == 17u) {
        static const uint16_t source_ids[] = { 24u, 16u, 15u, 17u };
        size_t source_index, src_off = 0, id0_off = 0, id1_off = 0;
        uint16_t id0_len = 0, id1_len = 0;
        int found_source = 0;
        for (source_index = 0;
             source_index < sizeof(source_ids) / sizeof(source_ids[0]);
             ++source_index) {
            src_off = 0;
            while (src_off + 4 <= *len) {
                uint16_t tl = get16(body + src_off);
                uint16_t id = get16(body + src_off + 2);
                size_t sub;
                if (tl < 4 || src_off + tl > *len) return -5;
                if (id != source_ids[source_index]) { src_off += tl; continue; }
                sub = src_off + 4;
                id0_len = id1_len = 0;
                while (sub + 4 <= src_off + tl) {
                    uint16_t sl = get16(body + sub);
                    uint16_t sid = get16(body + sub + 2);
                    if (sl < 4 || sub + sl > src_off + tl) return -3;
                    if (sid == 0u && sl == 6u) { id0_off = sub; id0_len = sl; }
                    if (sid == 1u && sl >= 5u && sl <= 68u) { id1_off = sub; id1_len = sl; }
                    sub += sl;
                }
                if (sub != src_off + tl) return -3;
                if (id0_len && id1_len) found_source = 1;
                break;
            }
            if (found_source) break;
        }
        if (!found_source) {
            altscreen_log("ERROR IAP2 ThemeAssets parent21 missing and no stock transport identity source; refusing synthesis");
            return -7;
        }
        {
            size_t group_len = 4u + (size_t)id0_len + (size_t)id1_len + 4u;
            size_t out = *len;
            if (group_len > 0xffffu || group_len > capacity - *len) return -6;
            put16(body + out, (uint16_t)group_len);
            put16(body + out + 2u, 21u);
            memcpy(body + out + 4u, body + id0_off, id0_len);
            memcpy(body + out + 4u + id0_len, body + id1_off, id1_len);
            put16(body + out + group_len - 4u, 4u);
            put16(body + out + group_len - 2u, 17u);
            *len += group_len;
            altscreen_log("IAP2 ThemeAssets synthesized parent=21 source_parent=%u copied_identity=0,1 bytes=%u",
                          source_ids[source_index], (unsigned)(id0_len + id1_len));
            return 1;
        }
    }

    /* Other diagnostic profiles may use the generic minimal parent form. */
    if (*len + 8 > capacity) return -6;
    put16(body + *len, 8);
    put16(body + *len + 2, p->param_id);
    put16(body + *len + 4, 4);
    put16(body + *len + 6, p->sub_id);
    *len += 8;
    return 1;
}

void altscreen_tlv_dump(const char *tag, const uint8_t *body, size_t len) {
    size_t off = 0;
    int params = 0;
    if (!body) return;
    altscreen_log("TLVTREE %s bytes=%u", tag ? tag : "-", (unsigned)len);
    while (off + 4 <= len) {
        uint16_t tl = get16(body + off), id = get16(body + off + 2);
        size_t sub;
        int subs = 0;
        if (tl < 4 || off + tl > len) {
            altscreen_log("TLVTREE %s stop=truncated at=0x%x", tag ? tag : "-", (unsigned)off);
            return;
        }
        altscreen_log("TLVTREE %s param[%d] id=%u len=%u", tag ? tag : "-", params, id, tl);
        sub = off + 4;
        while (sub + 4 <= off + tl) {
            uint16_t sl = get16(body + sub), sid = get16(body + sub + 2);
            unsigned first;
            if (sl < 4 || sub + sl > off + tl) break;
            first = (unsigned)(sub + 4 < off + tl ? body[sub + 4] : 0);
            if (subs < 24)
                altscreen_log("TLVTREE %s   sub[%d] id=%u len=%u first_payload=0x%02x",
                              tag ? tag : "-", subs, sid, sl, first);
            ++subs;
            sub += sl;
        }
        if (subs > 24)
            altscreen_log("TLVTREE %s   sub_count=%d truncated", tag ? tag : "-", subs);
        ++params;
        off += tl;
    }
}
