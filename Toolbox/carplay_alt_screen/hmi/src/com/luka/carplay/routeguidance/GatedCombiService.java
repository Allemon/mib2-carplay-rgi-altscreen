/*
 * GatedCombiService - Wrapper around CombiBAPServiceNavi that gates
 * native route-guidance updates during CarPlay route guidance.
 *
 * When blockRouteGuidance=true, native route-guidance related calls from
 * CombiBAPListener are silently dropped so they don't overwrite BAPBridge.
 * This keeps BAPBridge as the single source for FctIDs
 * 17/18/19/20/21/22/23/24/39/46/49/55.
 *
 * Non-route-guidance methods always delegate.
 *
 *
 */
package com.luka.carplay.routeguidance;

import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNavi;
import de.audi.atip.interapp.combi.bap.audio.data.CombiBAPTMCInfoMessage;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPDestinationInfo;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPDestinationListEntry;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPNaviDestination;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPNaviLaneGuidanceData;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPNaviManeuverDescriptor;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPSemiDynamicRouteInfo;
import de.audi.atip.interapp.combi.bap.navi.data.EtcStatus;
import com.luka.carplay.cluster.ClusterLayerController;
import com.luka.carplay.framework.Log;

public class GatedCombiService implements CombiBAPServiceNavi {
    final CombiBAPServiceNavi real;
    volatile boolean blockRouteGuidance;
    /* FctIDs 19/20/21/22/46 have different ownership from the rest of route guidance:
     * outside CarPlay RGI the stock navigator may keep driving the current/next-road
     * text, the lower bar,
     * travel information and destination menu; maneuver/lane records remain
     * session-gated. */
    volatile boolean blockCurrentPositionInfo;
    /* NO RGI text-only: the block covers only the route-text line (FctID 19/20);
     * stock keeps distance/arrival/destination (FctID 21/22/46). */
    volatile boolean currentPositionTextOnly;

    public GatedCombiService(CombiBAPServiceNavi r) { this.real = r; }

    /* Index into latest[]: 3 and 8..11 are the position fields, the rest route guidance. */
    private boolean fieldBlocked(int i) {
        if (i != 3 && i < 8) return blockRouteGuidance;
        return blockCurrentPositionInfo && (i == 3 || i == 8 || !currentPositionTextOnly);
    }

    /** Route-data ownership. BAPBridge is the only writer for the maneuver FctIDs while CarPlay RGI
     * is active.  Scale and altitude are NOT gated on this branch: the cluster shows the stock native
     * map, so its native scale/altitude readouts in the lower bar stay visible. */
    public synchronized void setRouteGuidanceBlocked(boolean active) {
        if (blockRouteGuidance && !active) markReplay(false);
        blockRouteGuidance = active;
        if (!active) replay(false);
    }

    public synchronized void setCurrentPositionInfoBlocked(boolean blocked) {
        if (blockCurrentPositionInfo && !blocked) markReplay(true);
        blockCurrentPositionInfo = blocked;
        if (!blocked) replay(true);
    }

    /* Applied before the block itself; narrowing an active block replays the released fields. */
    public synchronized void setCurrentPositionTextOnly(boolean textOnly) {
        boolean narrowing = blockCurrentPositionInfo && !currentPositionTextOnly && textOnly;
        currentPositionTextOnly = textOnly;
        if (narrowing) {
            for (int i=9; i<latest.length; i++) if (latest[i] != null) replayPending[i] = true;
            replay(true);
        }
    }

    private void markReplay(boolean position) {
        for (int i=0; i<latest.length; i++)
            if ((i==3 || i>=8) == position && latest[i] != null) replayPending[i] = true;
    }

    public synchronized void retire() {
        blockRouteGuidance = blockCurrentPositionInfo = false;
        for (int i=0; i<latest.length; i++) { latest[i]=null; replayPending[i]=false; }
    }

    /* Bounded OEM snapshot; mutations and replay run on NavigationJobs. */
    private final Object[][] latest = new Object[12][];
    private final boolean[] replayPending = new boolean[12];
    public synchronized boolean hasPendingReplay() {
        for (int i=0; i<replayPending.length; i++) if (replayPending[i]) return true;
        return false;
    }
    public synchronized void updateRGStatus(int a) {
        latest[0] = new Object[]{new Integer(a)};
        if (!blockRouteGuidance) { real.updateRGStatus(a); replayPending[0] = false; }
    }

    public synchronized void updateActiveRGType(int a) {
        latest[1] = new Object[]{new Integer(a)};
        if (!blockRouteGuidance) { real.updateActiveRGType(a); replayPending[1] = false; }
    }

    public synchronized void updateDistanceToNextManeuver(int a, int b, boolean c, int d) {
        latest[2] = new Object[]{new Integer(a), new Integer(b), (c ? Boolean.TRUE : Boolean.FALSE), new Integer(d)};
        if (!blockRouteGuidance) { real.updateDistanceToNextManeuver(a,b,c,d); replayPending[2] = false; }
    }

    public synchronized void updateCurrentPositionInfo(String s) {
        latest[3] = new Object[]{s};
        if (!blockCurrentPositionInfo) { real.updateCurrentPositionInfo(s); replayPending[3] = false; }
    }

    public synchronized void updateManeuverDescriptor(CombiBAPNaviManeuverDescriptor[] a) {
        latest[4] = new Object[]{a == null ? null : (CombiBAPNaviManeuverDescriptor[])a.clone()};
        if (!blockRouteGuidance) { real.updateManeuverDescriptor(a); replayPending[4] = false; }
    }

    public synchronized void updateLaneGuidance(boolean a, CombiBAPNaviLaneGuidanceData[] b) {
        latest[5] = new Object[]{(a ? Boolean.TRUE : Boolean.FALSE), b == null ? null : (CombiBAPNaviLaneGuidanceData[])b.clone()};
        if (!blockRouteGuidance) { real.updateLaneGuidance(a,b); replayPending[5] = false; }
    }

    public synchronized void updateExitView(int a, int b) {
        latest[6] = new Object[]{new Integer(a), new Integer(b)};
        if (!blockRouteGuidance) { real.updateExitView(a,b); replayPending[6] = false; }
    }

    public synchronized void updateManeuverState(int a) {
        latest[7] = new Object[]{new Integer(a)};
        if (!blockRouteGuidance) { real.updateManeuverState(a); replayPending[7] = false; }
    }

    public synchronized void updateTurnToInfo(String a, String b) {
        latest[8] = new Object[]{a, b};
        if (!blockCurrentPositionInfo) { real.updateTurnToInfo(a,b); replayPending[8] = false; }
    }

    public synchronized void updateDistanceToDestination(int a, int b, boolean c) {
        latest[9] = new Object[]{new Integer(a), new Integer(b), (c ? Boolean.TRUE : Boolean.FALSE)};
        if (!fieldBlocked(9)) { real.updateDistanceToDestination(a,b,c); replayPending[9] = false; }
    }

    public synchronized void updateTimeToDestination(int a, int b, long c) {
        latest[10] = new Object[]{new Integer(a), new Integer(b), new Long(c)};
        if (!fieldBlocked(10)) { real.updateTimeToDestination(a,b,c); replayPending[10] = false; }
    }

    public synchronized void updateDestinationInfo(CombiBAPDestinationInfo a) {
        latest[11] = new Object[]{a};
        if (!fieldBlocked(11)) { real.updateDestinationInfo(a); replayPending[11] = false; }
    }

    private long lastReplayWarning;
    private void replayWarning(Throwable t) {
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (lastReplayWarning != 0L && now >= lastReplayWarning && now - lastReplayWarning < 10000L) return;
        lastReplayWarning = now;
        try { logDisplayFailure(t); } catch (Throwable ignored) { }
    }

    private void replay(boolean position) {
        for (int i=0; i<latest.length; i++) {
            if (!replayPending[i] || latest[i] == null) continue;
            boolean isPosition = i==3 || i>=8;
            if (isPosition != position || fieldBlocked(i)) continue;
            Object[] v = latest[i];
            try {
                switch (i) {
                case 0: real.updateRGStatus(((Integer)v[0]).intValue()); break;
                case 1: real.updateActiveRGType(((Integer)v[0]).intValue()); break;
                case 2: real.updateDistanceToNextManeuver(((Integer)v[0]).intValue(), ((Integer)v[1]).intValue(), ((Boolean)v[2]).booleanValue(), ((Integer)v[3]).intValue()); break;
                case 3: real.updateCurrentPositionInfo((String)v[0]); break;
                case 4: real.updateManeuverDescriptor((CombiBAPNaviManeuverDescriptor[])v[0]); break;
                case 5: real.updateLaneGuidance(((Boolean)v[0]).booleanValue(), (CombiBAPNaviLaneGuidanceData[])v[1]); break;
                case 6: real.updateExitView(((Integer)v[0]).intValue(), ((Integer)v[1]).intValue()); break;
                case 7: real.updateManeuverState(((Integer)v[0]).intValue()); break;
                case 8: real.updateTurnToInfo((String)v[0], (String)v[1]); break;
                case 9: real.updateDistanceToDestination(((Integer)v[0]).intValue(), ((Integer)v[1]).intValue(), ((Boolean)v[2]).booleanValue()); break;
                case 10: real.updateTimeToDestination(((Integer)v[0]).intValue(), ((Integer)v[1]).intValue(), ((Long)v[2]).longValue()); break;
                case 11: real.updateDestinationInfo((CombiBAPDestinationInfo)v[0]); break;
                }
                replayPending[i] = false;
            } catch (Throwable t) { replayWarning(t); }
        }
    }

    /* All other methods: pure delegation */
    public void showInitializingScreen() { real.showInitializingScreen(); }
    public void hideInitializingScreen() { real.hideInitializingScreen(); }
    public void updateCompassInfo(int a, int b) { real.updateCompassInfo(a, b); }
    public void updateTMCInfoMessages(CombiBAPTMCInfoMessage[] a) {
        real.updateTMCInfoMessages(a); }
    public void updateLastDestinationsList(CombiBAPDestinationListEntry[] a) {
        real.updateLastDestinationsList(a); }
    public void updateFavoriteDestinationsList(CombiBAPDestinationListEntry[] a) {
        real.updateFavoriteDestinationsList(a); }
    public void updateHomeAddress(CombiBAPNaviDestination a) { real.updateHomeAddress(a); }
    public void routeGuidanceActDeactResult(int a) { real.routeGuidanceActDeactResult(a); }
    public void repeatLastNavAnnouncementResult(int a) {
        real.repeatLastNavAnnouncementResult(a); }
    public void updateVoiceGuidanceState(int a) { real.updateVoiceGuidanceState(a); }
    public void updateInfoStates(int a) { real.updateInfoStates(a); }
    public void updateTrafficBlockIndication(int a) { real.updateTrafficBlockIndication(a); }
    public void updateMapColor(int a) { real.updateMapColor(a); }
    public void updateMapType(int a, int b) { real.updateMapType(a, b); }
    public void updateSupportedMapTypes(boolean a, int b) { real.updateSupportedMapTypes(a, b); }
    public void updateMapView(int a, int b) { real.updateMapView(a, b); }
    public void updateSupportedMapViews(int a, int b) { real.updateSupportedMapViews(a, b); }
    public void updateMapVisibility(boolean lvdsVisible, boolean supplementaryVisible) {
        /* The second stock status bit is the cluster KDK visibility accepted by
         * CombiBAPListener. It must reach our layer even while native RGI is gated. */
        publishMapVisibility(supplementaryVisible);
        real.updateMapVisibility(lvdsVisible, supplementaryVisible);
    }

    public static void publishMapVisibility(boolean visible) {
        try { ClusterLayerController.onVcVisibility(visible); }
        catch (Throwable t) { logDisplayFailure(t); }
    }

    public static void publishMapPresentation(boolean largeMapView) {
        try { ClusterLayerController.onVcPresentation(largeMapView); }
        catch (Throwable t) { logDisplayFailure(t); }
    }

    private static void logDisplayFailure(Throwable t) {
        /* A local layer or logger failure must never suppress the OEM Status response. */
        try { Log.w("StatusGate", "cluster display-state forwarding failed: " + t); }
        catch (Throwable ignored) { }
    }
    public void updateMapOrientation(int a) { real.updateMapOrientation(a); }
    public void updateMapScale(int a, boolean b, int c, int d, boolean e) {
        real.updateMapScale(a, b, c, d, e);   /* native map scale — not gated on this branch */
    }
    public void updateAltitude(int a, int b) {
        real.updateAltitude(a, b);            /* native altitude — not gated on this branch */
    }
    public void updateOnlineNavigationState(int a, int b, int c) {
        real.updateOnlineNavigationState(a, b, c); }
    public void updateSemidynamicRouteGuidance(CombiBAPSemiDynamicRouteInfo a) {
        real.updateSemidynamicRouteGuidance(a); }
    public void poiSearchResult(int a, int b) { real.poiSearchResult(a, b); }
    public void updatePOIListSize(int a) { real.updatePOIListSize(a); }
    public void updateFSGSetup(int a, boolean b) { real.updateFSGSetup(a, b); }
    public void updateMapPresentation(boolean largeMapView, boolean leftMenu, boolean rightMenu) {
        publishMapPresentation(largeMapView);
        real.updateMapPresentation(largeMapView, leftMenu, rightMenu);
    }
    public void updateEtcStatus(EtcStatus a) { real.updateEtcStatus(a); }
}
