/*
 * altscreen_profile.h - one place that decides whether an outgoing iAP2
 * Identification message receives the ThemeAssets declaration.
 *
 * The K1004 policy is based on the MHI3/iOS reverse-engineered gate: top-level
 * transport component parameter 20 or 21, nested void subparameter 17. LIVI and
 * CatPlay provide the independently checked CSM framing and legacy transport
 * component layouts; neither treats USBHost subparameter 5 as ThemeAssets.
 *
 * observe is the safe setting: the frame is parsed and logged, and no byte is
 * changed. An unknown or unreadable profile name resolves to observe.
 */
#ifndef ALTSCREEN_PROFILE_H
#define ALTSCREEN_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#define ALTPROF_NAME_MAX 32

/* The identification parameter the declaration is attached to, and the
 * subparameter id that carries the capability. 0 means "no injection". */
struct altscreen_profile {
    const char *name;
    int         modifies_bytes;
    uint16_t    param_id;
    uint16_t    sub_id;
    const char *component;   /* which iAP2 component owns this declaration */
    /* NOTE: the 0x4300 CarPlayAvailability schema is fixed (param 2 / sub 0) and
     * deliberately has no field here: see p1404_iap2.h. */
};

extern const struct altscreen_profile altscreen_prof_observe;
extern const struct altscreen_profile altscreen_prof_theme_21_17;

/* Resolve by name; NULL or unknown returns the observe profile. */
const struct altscreen_profile *altscreen_profile_by_name(const char *name);

/* Currently selected profile. The process-safe default is observe; START must
 * explicitly select theme_21_17 and arm mutation before any byte is touched. */
const struct altscreen_profile *altscreen_profile_current(void);

/* Read the selection from a marker file whose contents are the profile name.
 * Called once at load; returns the resolved profile. Missing file, empty file or
 * unrecognised content all resolve deterministically (default / observe). */
const struct altscreen_profile *altscreen_profile_load(const char *path);

/* Same selection for the pure, host-testable entry point. */
void altscreen_profile_set(const struct altscreen_profile *p);

/*
 * Generic TLV advertisement: ensure <param_id>/<sub_id> exists in an
 * IdentificationInformation body.
 *
 *   1  a declaration was added (bytes grew)
 *   0  nothing to do: already present, or profile injects nothing
 *  <0  refused, body left untouched
 *
 * The body must be a complete parameter chain; a truncated or inconsistent chain
 * fails closed with no modification.
 */
int altscreen_tlv_advertise(uint8_t *body, size_t *len, size_t capacity,
                            const struct altscreen_profile *p);

/* Dump the TLV tree of a body into the log (control plane only, bounded). */
void altscreen_tlv_dump(const char *tag, const uint8_t *body, size_t len);

#endif /* ALTSCREEN_PROFILE_H */