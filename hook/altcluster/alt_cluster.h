/*
 * AltScreen cluster commands - CarPlay instrument-cluster display control.
 *
 * With the AltScreen mirror installed the iPhone renders a second CarPlay display
 * (the instrument cluster, AltScreen's fixed display UUID).  This module sends that
 * display the AirPlay session commands a factory cluster would:
 *
 *   CMD_ALT_ZOOM  [i8 MapScale steps]  -> "changeMapZoomLevel" {uuid, zoomDirection}
 *   CMD_ALT_UICTX [UTF-8 URL]          -> "showUI"             {uuid, url}
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

extern const hook_module_def_t alt_cluster_module_def;

#endif /* ALT_CLUSTER_H */
