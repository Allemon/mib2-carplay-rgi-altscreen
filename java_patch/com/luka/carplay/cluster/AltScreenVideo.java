/*
 * AltScreenVideo — is the AltScreen CarPlay cluster video on displayable 3?
 *
 * The AltScreen mirror sidecar (MHI2Q-CarPlay-AltScreen, carplay-alt111-mirror-display) draws
 * the CarPlay instrument-cluster stream into displayable 3.  Its launcher keeps the demand
 * marker while the video path is wanted and the sidecar publishes the ready marker only after
 * its first successful GLES present, so both together mean "displayable 3 carries live video".
 * Without AltScreen installed neither file exists and ScreenModule keeps the stock-map ctx 80.
 */
package com.luka.carplay.cluster;

import java.io.File;

public final class AltScreenVideo {

    public static final String ACTIVE_MARKER = "/tmp/mmi-mirror-active";
    public static final String READY_MARKER  = "/tmp/mmi-mirror-basevideo.ready";

    /* Host tests point these at scratch files. */
    private static String activePath = ACTIVE_MARKER;
    private static String readyPath  = READY_MARKER;

    private AltScreenVideo() {}

    public static boolean isReady() {
        try {
            return new File(activePath).exists() && new File(readyPath).exists();
        } catch (Throwable t) {
            return false;
        }
    }
}
