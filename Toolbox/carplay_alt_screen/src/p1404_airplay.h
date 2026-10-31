/*
 * p1404_airplay.h - AltScreen /info, SETUP negotiation and stream evidence.
 *
 * Exact P1404 facts are indexed by Research/AltScreen/P1404_ALTSCR_ABI_MAP.md.
 * ScreenStreamCreate is multi-instance, while the stock receiver owns one Main
 * ScreenSession slot. The second 111 ScreenSession belongs in the altscreen_state
 * side table and must never replace stock 110.
 */
#ifndef P1404_AIRPLAY_H
#define P1404_AIRPLAY_H
#include <stddef.h>
#include <stdint.h>

extern int alt_flag_info;
extern int alt_flag_feature;
extern int alt_flag_create111;
extern int alt_flag_iap2;

void p1404_airplay_bind(void);
void p1404_airplay_load_flags(void);
/* OEM cluster map startup gate: Java reports MapKombi.isInitialized() once
 * over bus 0x0119 (RGI library -> dlsym). Until then showUI/forceKeyFrame and
 * the display start wait; capabilities and Main110 are not delayed. */
int alt_oem_map_startup_ready(void);
void altscreen_oem_map_ready(int ready);
/* Phone maneuver card (INSTALL WITH CARD): 0 = map only, 1/2/3 = card
 * left/top/right. Only the FULL view of a known B9/Q7 layout shows the card. */
int alt_phone_card_mode(void);
int alt_request_cluster_after_main(void *receiver);
void *alt_build_cluster_display(void);
void *alt_info_add_cluster_display(void *container);
void *alt_advertise_features(void *value);
/* V3.5 cold-start contract: capability negotiation may use the measured
 * 1440x542 B9 bootstrap canvas before Screen display-1 is queryable.  The
 * private renderer still requires a live runtime geometry match. */
int alt_airplay_negotiation_geometry_ready(void);
int alt_airplay_validate_runtime_geometry(uint32_t width, uint32_t height);
void alt_note_control_command(void *session, void *cmd_name, void *arg);
void alt_note_stream_instance(void *stream, int created);
void alt_note_displays_container(const char *tag, void *container);
/* Returns the actual dictionary that carries type == target. */
void *alt_find_stream_descriptor(void *obj, uint32_t target_stream_type);
const char *alt_cf_cString(void *cfstring, char *buf, size_t cap);
/* Safe CFData byte view used by the exact _ScreenStreamSetProperty("avcc")
 * observer. The returned pointer remains owned by the stock CF object. */
int alt_cf_data_bytes(const void *obj, const uint8_t **data, size_t *bytes);
int alt_cf_is_array(const void *obj);
int alt_cf_is_dict(const void *obj);
int alt_cf_int64(const void *obj, int64_t *out);

/*
 * Full-chain CF transaction helpers implemented by p1404_airplay_fullchain.c.
 * They reuse the exact CF bindings already owned by the AirPlay translation unit,
 * so the P1404 private111 backend does not need a second dictionary/array schema
 * implementation or assumed raw convenience-helper ABI.
 */
int  alt_airplay_make_stock_request(void *receiver_session,
                                    const void *original_request,
                                    const void *alt_descriptor,
                                    void **owned_stock_request);
int  alt_airplay_merge_private_response(void *stock_response,
                                        void *private_stream_response);
void alt_airplay_release_object(void *object);

/* Narrow private111 bridge. All CF/CFL ABI-sensitive operations stay in the
 * fullchain translation unit where the exact P1404 adapters are already bound. */
int   alt_airplay_private_cf_ready(void);
int   alt_airplay_retain_object(void *object);
int   alt_airplay_get_int64_cstr(const void *dict, const char *key, int64_t *out);
void *alt_airplay_build_stream_response(uint32_t stream_type, uint16_t data_port);

#endif
