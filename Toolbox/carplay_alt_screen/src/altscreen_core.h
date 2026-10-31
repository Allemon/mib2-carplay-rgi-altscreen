#pragma once
#include <stddef.h>
#include <stdint.h>

#define CP_STREAM_MAIN_SCREEN 110
#define CP_STREAM_ALT_SCREEN  111
#define CP_FEATURE_ALTSCREEN   0x04000000ULL
/* The private stream is activated with display-specific showUI followed by
 * forceKeyFrame through the measured AirPlayReceiverSessionSendCommand ABI.
 * Both commands use the exact Alt UUID.
 *
 * Keep the cluster-map appearance vocabulary identical to the CarPlay
 * Simulator wire format: speed-limit/compass use user|no, ETA uses yes|no,
 * and maneuverLayout is present with an empty value in the captured map URL.
 * These flags only ask the phone/navigation app to include the corresponding
 * elements in the type111 navigation video; they do not alter the private111
 * transport, decoder, SHM ABI, renderer geometry, Context80, or wheel control.
 */
#define CP_ALT_CLUSTER_MAP_URL \
    "maps:/car/instrumentcluster/map?showSpeedLimit=user&showCompass=user&showETA=no&maneuverLayout="
/*
 * Phone maneuver card (INSTALL WITH CARD / INSTALL WITH RGI CARD, ported from
 * the Logo tree). The base cluster context lets the navigation app compose its
 * own maneuver card into the same type111 video; /map is map-only on some
 * providers. maneuverLayout places the card. leftaligned is accepted by the
 * phone but its on-screen result is still unconfirmed in a vehicle.
 */
#define CP_ALT_CLUSTER_CARD_URL_LEFT \
    "maps:/car/instrumentcluster?showSpeedLimit=user&showCompass=user&showETA=no&maneuverLayout=leftaligned"
#define CP_ALT_CLUSTER_CARD_URL_TOP \
    "maps:/car/instrumentcluster?showSpeedLimit=user&showCompass=user&showETA=no&maneuverLayout=topaligned"
#define CP_ALT_CLUSTER_CARD_URL_RIGHT \
    "maps:/car/instrumentcluster?showSpeedLimit=user&showCompass=user&showETA=no&maneuverLayout=rightaligned"

struct altscreen_display {
    const char *uuid;
    uint32_t stream_type;
    uint32_t width_pixels;
    uint32_t height_pixels;
    uint32_t width_mm;
    uint32_t height_mm;
    uint32_t max_fps;
};

/*
 * One field per independently provable fact. A field only flips when the exact
 * producer for that gate has succeeded; seeing a helper string or a request is
 * never enough to claim a later stage of the chain.
 */
struct altscreen_state {
    int theme_advertised;             /* G1A: HU sent the selected declaration */
    int theme_available;              /* G1B: phone confirmed it in 0x4300     */
    int info_alt_display;             /* G2 : /info carries an independent 111 */
    int info_alt_feature_advertised;  /* AUX: /info AltScreen bit reached phone*/
    int phone_requested_altscreen;    /* G3A: phone asked for it in SETUP      */
    int alt_feature_negotiated;       /* G3B: enabledFeatures really accepted  */
    int alt_ui_active;                /* G4 : UI/focus action proven or implicit*/
    int stream_111_setup;             /* G5B: private 111 receiver is live      */
    int alt_video_config_rx;          /* G6A: 111 VideoConfig/transport proven  */
    int alt_video_rx;                 /* LOG: bytes belong to private 111 stream*/
    int alt_decoder_ready;            /* LOG: stock decoder post observation    */
    int cockpit_altscreen;            /* G8 : cockpit source is private 111     */
    uint64_t alt_video_bytes;
    uint64_t alt_video_packets;
};

/* iAP2 IdentificationInformation body helpers. TLVs are BE16 length + BE16 id. */
int altscreen_iap2_add_theme_assets(uint8_t *buf, size_t *len, size_t capacity);
/*
 * Name of the ThemeAssets declaration profile in force. observe never changes
 * bytes; see altscreen_profile.h for why two readings coexist.
 */
const char *altscreen_profile_name(void);

/* State/logging API used by the QNX LD_PRELOAD adapter. */
void altscreen_init(void);
/* Start the bounded disk writer only after exact P1404 process identity passes. */
int altscreen_log_start_async(void);
void altscreen_log(const char *fmt, ...);
/* Stable transaction id persisted by Toolbox before the mandatory MMI reboot. */
const char *altscreen_run_id(void);
void altscreen_mark_theme_advertised(void);
void altscreen_mark_theme_available(int available);
void altscreen_mark_info_alt_feature_advertised(void);
void altscreen_mark_phone_requested(void);
void altscreen_mark_alt_feature_negotiated(void);
void altscreen_mark_info_alt_display(void);
void altscreen_mark_ui_active(const char *proof);
void altscreen_mark_stream_setup(uint32_t stream_type);
void altscreen_mark_video_config(const char *proof);
void altscreen_mark_video(uint32_t stream_type, size_t bytes);
void altscreen_mark_decoder_ready(const char *proof);
void altscreen_mark_cockpit_visible(const char *proof);
void altscreen_mark_cockpit_hidden(const char *proof);
const struct altscreen_state *altscreen_get_state(void);

/* Reference display declarations matching the open-source CarPlay implementations. */
const struct altscreen_display *altscreen_main_display(void);
const struct altscreen_display *altscreen_cluster_display(void);
int altscreen_set_cluster_geometry(uint32_t width, uint32_t height);
