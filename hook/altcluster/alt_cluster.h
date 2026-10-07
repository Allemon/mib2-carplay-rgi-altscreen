/*
 * AltScreen cluster commands - CarPlay instrument-cluster display control.
 *
 * With the AltScreen mirror installed the iPhone renders a second CarPlay display
 * (the instrument cluster, AltScreen's fixed display UUID).  This module sends that
 * display the AirPlay session commands a factory cluster would:
 *
 *   CMD_ALT_ZOOM  [i8 MapScale steps]  -> "changeMapZoomLevel" {uuid, zoomDirection}
 *   CMD_ALT_UICTX [UTF-8 URL]          -> "showUI"             {uuid, url}
 *   CMD_ALT_NIGHT [u8 0]               -> "setNightMode"       {nightMode}
 *
 * CMD_ALT_NIGHT: Google Maps starts the cluster display in its night look although the
 * head unit reported day (Apple Maps does not), and only the next light-switch
 * setNightMode corrects it.  The head unit gives the phone its day/night mode through
 * the "nightMode" session property at connect (seen by alt_viewarea.c's CopyProperty
 * interposer, recorded with alt_cluster_set_night); when the cluster video comes up,
 * Java sends CMD_ALT_NIGHT and the hook re-sends that same mode with the stock
 * setNightMode shape.  Nothing is sent when no mode was observed.
 *
 * The live AirPlayReceiverSession is tracked through the stock platform hooks
 * AirPlayReceiverSessionPlatformInitialize / ...Finalize (PLT-bound in libairplay,
 * not interposed by AltScreen); a command is sent under the same lock Finalize
 * takes, so it never reaches a freed session.
 */

#ifndef ALT_CLUSTER_H
#define ALT_CLUSTER_H

#include "../framework/hook_framework.h"
#include <stdint.h>

/* AltScreen publishes the cluster display under this fixed UUID
 * (libcarplay_altscreen.so; identical in every logged session). */
#define ALT_CLUSTER_DISPLAY_UUID "b7e6c5a0-2222-4000-8000-000000000002"

/* iOS DashBoard: changeMapZoomLevel.zoomDirection 0 = in, 1 = out. */
#define ALT_ZOOM_IN   0
#define ALT_ZOOM_OUT  1

/* Stock MapScale steps are added to the cluster map's zoom index, which grows
 * with the shown distance: a positive step zooms out. */
static inline int alt_zoom_direction_for_step(int step) {
    return step > 0 ? ALT_ZOOM_OUT : ALT_ZOOM_IN;
}

/* Day/night mode last reported to the phone: -1 unknown, 0 day, 1 night. */
void alt_cluster_set_night(int night);
int alt_cluster_night(void);
/* Switch file: when present, CMD_ALT_NIGHT is ignored. */
#ifndef ALT_NIGHT_SYNC_OFF
#define ALT_NIGHT_SYNC_OFF "/mnt/app/root/hooks/no_night_sync"
#endif

extern const hook_module_def_t alt_cluster_module_def;

#endif /* ALT_CLUSTER_H */
