/*
 * AltScreenCluster — commands for the CarPlay instrument-cluster display that the
 * AltScreen mirror shows on the VC (see AltScreenVideo).
 *
 *   Steering-wheel roller -> CMD_ALT_ZOOM: while the CarPlay video is on the cluster,
 *   each stock MapScale step also zooms the iPhone's cluster map (the hook sends the
 *   AirPlay "changeMapZoomLevel" command).  Stock still zooms its own hidden map.
 *
 *   Day/night -> CMD_ALT_NIGHT: Google Maps starts the cluster display in its night look
 *   although the head unit gave the phone day mode; when the video comes up the hook
 *   re-sends the mode the head unit reported (setNightMode), now and once more shortly
 *   after, because the phone builds the cluster UI a moment after the first frame.
 *
 *   Cluster UI context -> CMD_ALT_UICTX: if /mnt/app/root/hooks/cluster_ui.url holds a
 *   maps:/car/instrumentcluster URL, it is sent as "showUI" each time the video comes
 *   up, after AltScreen's own request.  The file is written by the MMI-Cockpit-Carplay
 *   GEM menu ("cluster layout"); without it AltScreen's default presentation stays.
 */
package com.luka.carplay.cluster;

import com.luka.carplay.bus.CarplayBus;
import com.luka.carplay.core.ScreenModule;
import com.luka.carplay.framework.Log;

import java.io.File;
import java.io.FileInputStream;

public final class AltScreenCluster {

    private static final String TAG = "AltCluster";
    public static final String UI_URL_FILE = "/mnt/app/root/hooks/cluster_ui.url";
    private static final String URL_PREFIX = "maps:";
    private static final int MAX_URL = 400;

    /* Host tests point this at a scratch file. */
    private static String uiUrlPath = UI_URL_FILE;

    private AltScreenCluster() {}

    /** Stock CombiBAPListener.setMapScale steps (positive = zoom out). */
    public static void onMapScaleSteps(int steps) {
        if (steps == 0 || !ScreenModule.isAltScreenVideo()) return;
        if (steps > 127) steps = 127;
        if (steps < -127) steps = -127;
        boolean sent = CarplayBus.getInstance().sendBinary(CarplayBus.CMD_ALT_ZOOM,
            new byte[] { (byte)steps });
        Log.i(TAG, "roller steps=" + steps + " -> cluster zoom " + (steps > 0 ? "out" : "in")
            + (sent ? "" : " (bus down)"));
    }

    /** ScreenModule: the AltScreen video just came up on the cluster. */
    public static void onVideoReady() {
        AltScreenPlacement.onVideoReady();
        sendNightSync();
        scheduleNightSync();
        String url = readUiUrl();
        if (url == null) return;
        byte[] bytes;
        try { bytes = url.getBytes("UTF-8"); }
        catch (Throwable t) { return; }
        boolean sent = CarplayBus.getInstance().sendBinary(CarplayBus.CMD_ALT_UICTX, bytes);
        Log.i(TAG, "cluster showUI " + url + (sent ? "" : " (bus down)"));
    }

    /** Asks the hook to re-send the day/night mode to the phone (no-op without a known mode). */
    static void sendNightSync() {
        boolean sent = CarplayBus.getInstance().sendBinary(CarplayBus.CMD_ALT_NIGHT, new byte[] { 0 });
        Log.i(TAG, "cluster day/night sync" + (sent ? "" : " (bus down)"));
    }

    /** One more request ~3 s later; only the latest video start may fire it. */
    private static int nightSyncGeneration;

    private static void scheduleNightSync() {
        final int generation;
        synchronized (AltScreenCluster.class) { generation = ++nightSyncGeneration; }
        Thread t = new Thread(new Runnable() {
            public void run() {
                try { Thread.sleep(3000L); } catch (InterruptedException e) { return; }
                synchronized (AltScreenCluster.class) {
                    if (generation != nightSyncGeneration || !ScreenModule.isAltScreenVideo()) return;
                }
                sendNightSync();
            }
        }, "AltNightSync");
        t.setDaemon(true);
        t.start();
    }

    static String readUiUrl() {
        FileInputStream in = null;
        try {
            File f = new File(uiUrlPath);
            if (!f.exists() || f.length() <= 0 || f.length() > MAX_URL) return null;
            byte[] buf = new byte[(int)f.length()];
            in = new FileInputStream(f);
            int n = 0;
            while (n < buf.length) {
                int r = in.read(buf, n, buf.length - n);
                if (r < 0) break;
                n += r;
            }
            String url = new String(buf, 0, n, "UTF-8").trim();
            if (!url.startsWith(URL_PREFIX)) {
                Log.w(TAG, "ignoring " + uiUrlPath + ": not a maps: URL");
                return null;
            }
            return url;
        } catch (Throwable t) {
            return null;
        } finally {
            if (in != null) try { in.close(); } catch (Throwable t) { }
        }
    }
}
