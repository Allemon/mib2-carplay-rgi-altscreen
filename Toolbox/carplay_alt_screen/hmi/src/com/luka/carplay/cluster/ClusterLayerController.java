/*
 * ClusterLayerController — the single owner of the CarPlay cluster plane geometry.
 *
 * Extracted out of the stock CombiMapController so the stock class carries only a one-line call-out
 * (patch footprint = minimal, survives a firmware re-decompile).  All CarPlay cluster-plane policy
 * lives here:
 *
 *   98  = maneuver_render window (Software, 328x181)   — our RGI maneuver
 *   101 = stock 987 KDK backing  (Image, 328x180)      — sport/full size
 *   102 = stock 987 KDK backing  (Image, 210x153)      — popup size
 *
 * While CarPlay shows a route the stage's backing (101 in the tube, 102 in the popup) sits under
 * the arrow and follows its opacity, as before V3.8 Fix1, so the map does not show through the
 * maneuver panel.  MMI-Cockpit-Carplay fork: state/rgi-backing-off (GEM "RGI backing: off") keeps
 * both backings at opacity 0 instead (V3.8 Fix1: the arrow sits directly on the map).  Stock gets
 * its backing model back on release.
 *
 * CarPlay ownership/navigation gates eligibility; VC FctID44 gates actual visibility,
 * and VC FctID54 selects the KDK stage. The requested View mode never reveals a layer.
 * The arrow fades with the VC's own 200 ms linear KDK animation instead of popping: FctID44
 * withdraws visibility at 30 % of the VC fade-out (the remaining 140 ms is ours) and returns it
 * just before the VC reopens the KDK.  A visible arrow is never moved; it is hidden first.
 * Q7: the cluster may never send FctID44 for a CarPlay route.  Like stock's Q7-FPK fallback
 * (ClusterKDKHandlerImpl: "did not get request from Kombi in time... setting expected KDK
 * visibility"), the arrow is shown when no FctID44 has arrived 1.5 s after guidance began.
 * Stock hints are retained for restoration when CarPlay releases the cluster.  ctx 80/81 (ScreenModule) already removes the layers from composition on nav-off; the
 * opacity=0 here is the belt to that suspenders.
 *
 * Geometry comes from the terminal's active stock Layout.  This is important on B9: Classic and
 * Sport use different in-tube anchors/crops, and the stock skin switch changes the Layout object at
 * runtime.  We cache primitive values rather than the Layout itself so reapply() remains safe after
 * a context transition.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.cluster;

import com.luka.carplay.framework.Log;
import de.audi.tghu.fwhmi.IDisplayManagerKombiControl;
import de.esolutions.hmi.widgets.audi.base.Layout;

public final class ClusterLayerController {

    /* CarPlay cluster displayables (see dc[80] = {98,101,102,99} in DisplayManagerMIB2High). */
    private static final int MANEUVER      = 98;    /* maneuver_render (Software) */
    private static final int BACKING_SPORT = 101;   /* 987 KDK backing, 328x180 */
    private static final int BACKING_POPUP = 102;   /* 987 KDK backing, 210x153 */
    /* Stock CombiMapController/Layout slots.  Names describe the KDK stage, not the skin: the
     * in-tube values themselves differ between LayoutMIB2HighB9 and LayoutMIB2HighB9Sport. */
    private static final int LC_IN_TUBE_X = 58, LC_IN_TUBE_Y = 59;
    private static final int LC_POPUP_X = 60, LC_POPUP_Y = 61;
    private static final int LC_POPUP_CROP_X = 118, LC_POPUP_CROP_Y = 119;
    private static final int LC_POPUP_CROP_W = 120, LC_POPUP_CROP_H = 121;
    private static final int LC_IN_TUBE_CROP_X = 122, LC_IN_TUBE_CROP_Y = 123;
    private static final int LC_IN_TUBE_CROP_W = 124, LC_IN_TUBE_CROP_H = 125;
    /* Stock map-only offset, recorded for diagnostics; never applied to KDK. */
    private static final int LC_SMALL_STAGE_DX = 80, LC_SMALL_STAGE_DY = 81;
    /* Full-range durations; a reversed fade takes the share it still has to cover. */
    private static final int FADE_IN_MS = 200, FADE_OUT_MS = 140, FADE_STEP_MS = 33;
    private static final long Q7_KDK_FALLBACK_MS = 1500L;
    private static final Object LOCK = new Object();
    private static final Object APPLY_LOCK = new Object();
    /* Arrow fade and Q7 fallback state; guarded by APPLY_LOCK like every DM write. */
    private static int maneuverOpacity = -1;       /* last value written to 98, -1 unknown */
    private static int fadeBacking;                /* backing that follows 98, 0 = none */
    /* Fork switch, re-read at most every BACKING_CHECK_MS (applyNow runs on HMI threads). */
    private static final String BACKING_OFF_FILE = "/mnt/app/root/carplay-altscreen/state/rgi-backing-off";
    private static final long BACKING_CHECK_MS = 2000L;
    private static boolean backingOff;
    private static long backingCheckedAt;
    private static boolean fading;
    private static int fadeFrom, fadeTo, fadeDuration, fadeTerminal;
    private static long fadeStart;
    private static IDisplayManagerKombiControl fadeDm;
    private static String lastCrop;
    private static long guidanceSince;             /* 0 = CarPlay not guiding */
    private static long q7FallbackAt;              /* 0 = nothing scheduled */
    private static boolean q7FallbackLogged;
    private static final Object FADE_WAKE = new Object();
    private static boolean fadeWakePending;
    private static Thread fader;
    private static IDisplayManagerKombiControl lastDm;
    private static int lastTerminal;
    private static boolean lastStockPopup = true;
    private static boolean vcPopup = true;
    private static boolean haveVcStage;
    private static boolean vcVisible;
    private static boolean haveVcVisibility;
    private static boolean lastStockVisible;
    private static int lastStockOpacity;
    /* Safe fallback used only before stock CombiMapController publishes its live Layout. */
    private static Geometry lastGeometry = new Geometry(
        984, 139, 1091, 110,
        59, 27, 210, 153,
        0, 0, 328, 180,
        -476, 0,
        "fallback-sport");
    private static boolean haveLayout;
    private static boolean errorLogged;
    private static String lastAppliedSignature;
    private static volatile ViewportListener viewportListener;

    public interface ViewportListener {
        void onManeuverViewportChanged();
    }

    public static void setViewportListener(ViewportListener listener) { viewportListener = listener; }
    public static void clearViewportListener(ViewportListener listener) {
        if (viewportListener == listener) viewportListener = null;
    }

    /** Source pixels actually visible on VC, in the renderer's 328x181 frame.
     * Use the same stage selection and crop as applyNow(), without moving the plane. */
    public static int[] maneuverViewport() {
        synchronized (LOCK) {
            boolean popup = haveVcStage ? vcPopup : lastStockPopup;
            Geometry g = lastGeometry;
            return popup ? new int[]{g.popupCropX, g.popupCropY, g.popupCropW, g.popupCropH}
                : new int[]{g.inTubeCropX, g.inTubeCropY, g.inTubeCropW, g.inTubeCropH};
        }
    }

    private ClusterLayerController() {}

    private static final class Geometry {
        final int inTubeX, inTubeY, popupX, popupY;
        final int popupCropX, popupCropY, popupCropW, popupCropH;
        final int inTubeCropX, inTubeCropY, inTubeCropW, inTubeCropH;
        final int smallStageDX, smallStageDY;
        final String layoutName;

        Geometry(int inTubeX, int inTubeY, int popupX, int popupY,
                 int popupCropX, int popupCropY, int popupCropW, int popupCropH,
                 int inTubeCropX, int inTubeCropY, int inTubeCropW, int inTubeCropH,
                 int smallStageDX, int smallStageDY,
                 String layoutName) {
            this.inTubeX = inTubeX;
            this.inTubeY = inTubeY;
            this.popupX = popupX;
            this.popupY = popupY;
            this.popupCropX = popupCropX;
            this.popupCropY = popupCropY;
            this.popupCropW = popupCropW;
            this.popupCropH = popupCropH;
            this.inTubeCropX = inTubeCropX;
            this.inTubeCropY = inTubeCropY;
            this.inTubeCropW = inTubeCropW;
            this.inTubeCropH = inTubeCropH;
            this.smallStageDX = smallStageDX;
            this.smallStageDY = smallStageDY;
            this.layoutName = layoutName;
        }

        boolean sameValues(Geometry other) {
            return other != null
                && inTubeX == other.inTubeX && inTubeY == other.inTubeY
                && popupX == other.popupX && popupY == other.popupY
                && popupCropX == other.popupCropX && popupCropY == other.popupCropY
                && popupCropW == other.popupCropW && popupCropH == other.popupCropH
                && inTubeCropX == other.inTubeCropX && inTubeCropY == other.inTubeCropY
                && inTubeCropW == other.inTubeCropW && inTubeCropH == other.inTubeCropH
                && smallStageDX == other.smallStageDX && smallStageDY == other.smallStageDY;
        }
    }

    private static Geometry geometryFrom(Layout layout) {
        return new Geometry(
            layout.getIntegerConstant(LC_IN_TUBE_X),
            layout.getIntegerConstant(LC_IN_TUBE_Y),
            layout.getIntegerConstant(LC_POPUP_X),
            layout.getIntegerConstant(LC_POPUP_Y),
            layout.getIntegerConstant(LC_POPUP_CROP_X),
            layout.getIntegerConstant(LC_POPUP_CROP_Y),
            layout.getIntegerConstant(LC_POPUP_CROP_W),
            layout.getIntegerConstant(LC_POPUP_CROP_H),
            layout.getIntegerConstant(LC_IN_TUBE_CROP_X),
            layout.getIntegerConstant(LC_IN_TUBE_CROP_Y),
            layout.getIntegerConstant(LC_IN_TUBE_CROP_W),
            layout.getIntegerConstant(LC_IN_TUBE_CROP_H),
            layout.getIntegerConstant(LC_SMALL_STAGE_DX),
            layout.getIntegerConstant(LC_SMALL_STAGE_DY),
            layout.getClass().getName());
    }

    /** Seed/cache the exact OEM geometry even before the first KDK model delta. */
    public static void updateLayout(IDisplayManagerKombiControl dm, int terminal, Layout layout) {
        if (dm == null || layout == null) return;
        Geometry geometry;
        try {
            geometry = geometryFrom(layout);
        } catch (Throwable t) {
            Log.w("ClusterLayers", "layout read failed: " + t);
            return;
        }
        boolean changed;
        synchronized (LOCK) {
            changed = !geometry.sameValues(lastGeometry)
                || !geometry.layoutName.equals(lastGeometry.layoutName);
            lastDm = dm;
            lastTerminal = terminal;
            lastGeometry = geometry;
            haveLayout = true;
        }
        if (changed) {
            ViewportListener listener = viewportListener;
            if (listener != null) {
                try { listener.onManeuverViewportChanged(); }
                catch (Throwable t) { Log.w("ClusterLayers", "viewport listener failed: " + t); }
            }
            Log.i("ClusterLayers", "layout=" + geometry.layoutName
                + " inTube=(" + geometry.inTubeX + "," + geometry.inTubeY + ") crop=("
                + geometry.inTubeCropX + "," + geometry.inTubeCropY + ","
                + geometry.inTubeCropW + "x" + geometry.inTubeCropH + ") popup=("
                + geometry.popupX + "," + geometry.popupY + ") crop=("
                + geometry.popupCropX + "," + geometry.popupCropY + ","
                + geometry.popupCropW + "x" + geometry.popupCropH + ")"
                + " smallStage=(" + geometry.smallStageDX + "," + geometry.smallStageDY + ")");
        }
    }

    /** Cold-boot fallback before the first stock KDK model update. The first real update replaces
     * the conservative popup geometry with the authoritative popup/in-tube layout. */
    public static void bind(IDisplayManagerKombiControl dm, int terminal) {
        synchronized (LOCK) {
            if (lastDm != dm) {
                lastStockVisible = false;
                lastStockOpacity = 0;
            }
            lastDm = dm;
            lastTerminal = terminal;
            haveLayout = true;
        }
    }

    /** Single source for acknowledged KDK visibility, independent of CarPlay sessions. */
    public static boolean isKdkVisible() {
        synchronized (LOCK) {
            return haveVcVisibility ? vcVisible : lastStockVisible && lastStockOpacity > 0;
        }
    }

    /** Receive accepted FctID44 state before the stock listener sends its Status response. */
    public static void onVcVisibility(boolean visible) {
        synchronized (LOCK) {
            vcVisible = visible;
            haveVcVisibility = true;
        }
        Log.i("ClusterLayers", "VC Fct44 KDK visible=" + visible);
        reapply();
        com.luka.carplay.core.ScreenModule.onVcKdkVisibility(visible);
    }

    /** Receive VC FctID54, emitted at the stage animation midpoint. */
    public static void onVcPresentation(boolean largeMapView) {
        boolean changed;
        synchronized (LOCK) {
            changed = !haveVcStage || vcPopup != largeMapView;
            vcPopup = largeMapView;
            haveVcStage = true;
        }
        Log.i("ClusterLayers", "VC Fct54 KDK stage=" + (largeMapView ? "popup" : "inTube"));
        reapply();
        if (changed) {
            ViewportListener listener = viewportListener;
            if (listener != null) {
                try { listener.onManeuverViewportChanged(); }
                catch (Throwable t) { Log.w("ClusterLayers", "viewport listener failed: " + t); }
            }
        }
    }

    /** Cache stock hints for normal-navigation restoration. CarPlay uses the same VC
     * visibility/presentation inputs, captured before stock availability can mask them. */
    public static void apply(IDisplayManagerKombiControl dm, int terminal, Layout layout,
                             boolean stockVisible, int stockOpacity, boolean inTube) {
        updateLayout(dm, terminal, layout);
        synchronized (LOCK) {
            lastDm = dm;
            lastTerminal = terminal;
            lastStockPopup = !inTube;
            lastStockVisible = stockVisible;
            lastStockOpacity = stockOpacity;
            haveLayout = true;
        }
        reapply();
    }

    /** ABI retained by the existing firmware CombiMapController. */
    public static void apply(IDisplayManagerKombiControl dm, int terminal, Layout layout,
                             boolean popup, boolean stockVisible, int stockOpacity) {
        apply(dm, terminal, layout, stockVisible, stockOpacity, !popup);
    }

    /** Re-apply the last stock KDK geometry after ScreenModule changes ctx 80/81. */
    public static void reapply() {
        synchronized (APPLY_LOCK) { reapplySerialized(); }
    }

    private static void reapplySerialized() {
        IDisplayManagerKombiControl dm;
        int terminal;
        boolean stockVisible;
        int stockOpacity;
        Geometry geometry;
        synchronized (LOCK) {
            if (!haveLayout || lastDm == null) return;
            dm = lastDm;
            terminal = lastTerminal;
            stockVisible = lastStockVisible;
            stockOpacity = lastStockOpacity;
            geometry = lastGeometry;
        }
        applyNow(dm, terminal, geometry, stockVisible, stockOpacity);
    }

    private static void applyNow(IDisplayManagerKombiControl dm, int terminal, Geometry geometry,
                                 boolean stockVisible, int stockOpacity) {
        boolean popup;
        boolean carplayOwnsCluster = com.luka.carplay.core.ScreenModule.isConnected();
        boolean navActive = com.luka.carplay.core.ScreenModule.isNavActive();
        boolean vcReported;
        int permittedOpacity;
        synchronized (LOCK) {
            popup = carplayOwnsCluster && haveVcStage ? vcPopup : lastStockPopup;
            vcReported = haveVcVisibility;
            permittedOpacity = haveVcVisibility ? (vcVisible ? 100 : 0)
                : (stockVisible ? stockOpacity : 0);
        }
        long now = System.currentTimeMillis();
        if (!carplayOwnsCluster || !navActive) {
            guidanceSince = 0L;
            q7FallbackLogged = false;
        } else if (guidanceSince == 0L || now < guidanceSince) {
            guidanceSince = now;
        }
        q7FallbackAt = 0L;
        if (guidanceSince != 0L && !vcReported && geometry.layoutName.indexOf("Q7") >= 0) {
            long due = guidanceSince + Q7_KDK_FALLBACK_MS;
            if (now >= due) {
                permittedOpacity = 100;
                if (!q7FallbackLogged) {
                    q7FallbackLogged = true;
                    Log.w("ClusterLayers", "Q7 KDK fallback: no VC FctID44 " + Q7_KDK_FALLBACK_MS
                        + " ms after CarPlay guidance began, showing the arrow");
                }
            } else {
                q7FallbackAt = due;
            }
        }
        /* Do NOT apply the layout's small-stage offset (80/81) here.  Stock adds it to the map
         * planes 33/58 only; the KDK panel and its backing have no view-size dependency at all
         * (positionKDKBackgrounds / handleKdkDualTerminal read no view size).  Moving the panel
         * by -476 in Sport singlescreen was measured on the car to break a view that stock keeps
         * correct.  The offset is logged below for diagnosis, never applied. */
        int carplayOpacity = carplayOwnsCluster && navActive ? permittedOpacity : 0;
        logDecision(geometry, popup, carplayOpacity);
        try {
            /* 101/102 are shared with the stock KDK renderer.  Restore the last stock model
             * when CarPlay releases terminal 1; otherwise a disconnect can leave
             * Audi navigation's backing permanently transparent until an unrelated KDK delta. */
            if (!carplayOwnsCluster) {
                fading = false;
                lastCrop = null;
                maneuverOpacity = 0;
                fadeBacking = 0;
                dm.setOpacity(MANEUVER, terminal, 0);
                if (stockVisible) {
                    dm.setOpacity(popup ? BACKING_POPUP : BACKING_SPORT,
                                  terminal, stockOpacity);
                    dm.setOpacity(popup ? BACKING_SPORT : BACKING_POPUP,
                                  terminal, 0);
                } else {
                    dm.setOpacity(BACKING_SPORT, terminal, 0);
                    dm.setOpacity(BACKING_POPUP, terminal, 0);
                }
                errorLogged = false;
                return;
            }
            if (!navActive) {
                fading = false;
                lastCrop = null;
                maneuverOpacity = 0;
                fadeBacking = 0;
                dm.setOpacity(MANEUVER, terminal, 0);
                dm.setOpacity(BACKING_SPORT, terminal, 0);
                dm.setOpacity(BACKING_POPUP, terminal, 0);
                return;
            }
            // One composition path for both stock stages; visibility is independent.
            int cx = popup ? geometry.popupCropX : geometry.inTubeCropX;
            int cy = popup ? geometry.popupCropY : geometry.inTubeCropY;
            int cw = popup ? geometry.popupCropW : geometry.inTubeCropW;
            int ch = popup ? geometry.popupCropH : geometry.inTubeCropH;
            int dx = popup ? geometry.popupX : geometry.inTubeX;
            int dy = popup ? geometry.popupY : geometry.inTubeY;
            String crop = cx + "," + cy + " " + cw + "x" + ch + "@" + dx + "," + dy + "/" + terminal;
            if (!crop.equals(lastCrop) && maneuverOpacity != 0) {
                /* FctID54 lands mid-morph, normally after FctID44 hid the arrow.  If it is
                 * still visible, cut it rather than slide it across the cluster. */
                fading = false;
                dm.setOpacity(MANEUVER, terminal, 0);
                if (fadeBacking != 0) dm.setOpacity(fadeBacking, terminal, 0);
                maneuverOpacity = 0;
            }
            dm.setCropping(MANEUVER, terminal, cx, cy, cw, ch, dx, dy, cw, ch);
            lastCrop = crop;
            int backing = popup ? BACKING_POPUP : BACKING_SPORT;
            int otherBacking = popup ? BACKING_SPORT : BACKING_POPUP;
            if (backingWanted(now)) {
                /* Pre-Fix1 placement: the stage backing at the arrow's destination, same opacity. */
                dm.setPosition(backing, terminal, dx, dy);
                dm.setOpacity(backing, terminal, maneuverOpacity > 0 ? maneuverOpacity : 0);
                dm.setOpacity(otherBacking, terminal, 0);
                fadeBacking = backing;
            } else {
                fadeBacking = 0;
                dm.setOpacity(BACKING_SPORT, terminal, 0);
                dm.setOpacity(BACKING_POPUP, terminal, 0);
            }
            fadeManeuverTo(dm, terminal, carplayOpacity, now);
            errorLogged = false;
        } catch (Throwable t) {
            /* Never throw into HMI/DM threads, but keep the first failure diagnosable. */
            if (!errorLogged) {
                errorLogged = true;
                Log.w("ClusterLayers", "apply failed: " + t);
            }
        }
        if (q7FallbackAt != 0L) wakeFader();
    }

    /** Called under APPLY_LOCK.  True unless the fork's rgi-backing-off switch file exists. */
    private static boolean backingWanted(long now) {
        if (backingCheckedAt == 0L || now < backingCheckedAt
                || now - backingCheckedAt >= BACKING_CHECK_MS) {
            boolean off;
            try { off = new java.io.File(BACKING_OFF_FILE).exists(); }
            catch (Throwable t) { off = false; }
            if (off != backingOff || backingCheckedAt == 0L)
                Log.i("ClusterLayers", "RGI backing " + (off ? "off (" + BACKING_OFF_FILE + ")" : "on"));
            backingOff = off;
            backingCheckedAt = now;
        }
        return !backingOff;
    }

    /** Called under APPLY_LOCK.  Starts (or retargets) the ramp of 98 toward target. */
    private static void fadeManeuverTo(IDisplayManagerKombiControl dm, int terminal, int target,
                                       long now) {
        if (fading && fadeTo == target && fadeDm == dm && fadeTerminal == terminal) return;
        int from = maneuverOpacity;
        if (fading) {
            stepFade(now);      /* resume from the value on screen, not the old start */
            from = maneuverOpacity;
        }
        fading = false;
        if (from < 0) {
            dm.setOpacity(MANEUVER, terminal, 0);
            if (fadeBacking != 0) dm.setOpacity(fadeBacking, terminal, 0);
            maneuverOpacity = from = 0;
        }
        if (from == target) return;
        int span = target > from ? target - from : from - target;
        fadeFrom = from;
        fadeTo = target;
        fadeStart = now;
        fadeDuration = (target > from ? FADE_IN_MS : FADE_OUT_MS) * span / 100;
        fadeDm = dm;
        fadeTerminal = terminal;
        fading = true;
        wakeFader();
    }

    /** Called under APPLY_LOCK.  Writes the current ramp value; ends the ramp at its target. */
    private static void stepFade(long now) {
        if (!fading) return;
        long elapsed = now - fadeStart;
        int value;
        if (elapsed < 0L || elapsed >= fadeDuration) {      /* a clock jump ends the ramp */
            value = fadeTo;
            fading = false;
        } else {
            value = fadeFrom + (int) ((fadeTo - fadeFrom) * elapsed / fadeDuration);
        }
        try {
            fadeDm.setOpacity(MANEUVER, fadeTerminal, value);
            if (fadeBacking != 0) fadeDm.setOpacity(fadeBacking, fadeTerminal, value);
            maneuverOpacity = value;
        } catch (Throwable t) {
            fading = false;
            if (!errorLogged) {
                errorLogged = true;
                Log.w("ClusterLayers", "fade failed: " + t);
            }
        }
    }

    private static void wakeFader() {
        synchronized (FADE_WAKE) {
            fadeWakePending = true;
            if (fader == null) {
                fader = new Thread(new Runnable() {
                    public void run() { fadeLoop(); }
                }, "cluster-arrow-fade");
                fader.setDaemon(true);
                fader.start();
            }
            FADE_WAKE.notifyAll();
        }
    }

    /* Own thread: DM writes only, no file I/O.  Sleeps until a ramp or the Q7 deadline. */
    private static void fadeLoop() {
        while (true) {
            long wait = 0L;
            boolean q7Due = false;
            try {
                synchronized (APPLY_LOCK) {
                    long now = System.currentTimeMillis();
                    stepFade(now);
                    if (fading) wait = FADE_STEP_MS;
                    if (q7FallbackAt != 0L) {
                        long left = q7FallbackAt - now;
                        if (left <= 0L) {
                            q7FallbackAt = 0L;
                            q7Due = true;
                        } else if (wait == 0L || left < wait) {
                            wait = left;
                        }
                    }
                }
                if (q7Due) {
                    reapply();
                    continue;
                }
                synchronized (FADE_WAKE) {
                    if (!fadeWakePending) {
                        if (wait == 0L) FADE_WAKE.wait();
                        else FADE_WAKE.wait(wait);
                    }
                    fadeWakePending = false;
                }
            } catch (InterruptedException e) {
                synchronized (FADE_WAKE) { fader = null; }
                return;
            } catch (Throwable t) {
                if (!errorLogged) {
                    errorLogged = true;
                    Log.w("ClusterLayers", "fade loop: " + t);
                }
                try { Thread.sleep(FADE_STEP_MS); }     /* never spin on a repeating failure */
                catch (InterruptedException e) {
                    synchronized (FADE_WAKE) { fader = null; }
                    return;
                }
            }
        }
    }

    /** One line per distinct geometry decision — the exact numbers written to the DM.
     *  Every Classic/Sport/singlescreen bug so far was a guess about which branch ran; this makes
     *  it readable in /tmp/carplay_java.log instead. Logged only when the tuple changes. */
    private static void logDecision(Geometry g, boolean popup, int opacity) {
        int cropX = popup ? g.popupCropX : g.inTubeCropX;
        int cropY = popup ? g.popupCropY : g.inTubeCropY;
        int cropW = popup ? g.popupCropW : g.inTubeCropW;
        int cropH = popup ? g.popupCropH : g.inTubeCropH;
        int dstX  = popup ? g.popupX     : g.inTubeX;
        int dstY  = popup ? g.popupY     : g.inTubeY;

        String line = "apply " + g.layoutName
            + " view=" + (com.luka.carplay.core.ScreenModule.isSmallScreenViewArea()
                          ? "single" : "full")
            + " stage=" + (popup ? "popup" : "inTube")
            + " backing=" + (popup ? BACKING_POPUP : BACKING_SPORT)
            + " opacity=" + opacity
            + " src=(" + cropX + "," + cropY + " " + cropW + "x" + cropH + ")"
            + " dst=(" + dstX + "," + dstY + ")"
            + " smallStageOffset=(" + g.smallStageDX + "," + g.smallStageDY + ") [not applied]";

        /* Every value that can change the picture is in the line, so comparing the line itself
         * is the dedup key. */
        synchronized (LOCK) {
            if (line.equals(lastAppliedSignature)) return;
            lastAppliedSignature = line;
        }
        Log.i("ClusterLayers", line);
    }

}
