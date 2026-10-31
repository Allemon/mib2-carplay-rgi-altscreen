/*
 * ScreenNavStatusGate - installs the single wrapper that prevents stock
 * route-guidance BAP writes from racing CarPlay RGI.  It deliberately does not
 * alter NAVSD availability, initializing state, compass, scale or map status.
 */
package com.luka.carplay.core;

import com.luka.carplay.framework.Log;
import com.luka.carplay.routeguidance.GatedCombiService;

import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNavi;
import de.audi.tghu.navi.app.Navigation;
import de.audi.tghu.navi.app.cluster.ClusterService;
import java.lang.reflect.Field;

public final class ScreenNavStatusGate {
    private static final String TAG = "StatusGate";

    private static ClusterService clusterService;
    private static GatedCombiService gate;
    private static boolean desiredRouteBlocked;
    private static boolean appliedRouteBlocked;
    private static boolean desiredCurrentPositionBlocked;
    private static boolean desiredCurrentPositionTextOnly;
    private static boolean installScheduled;
    private static long installScheduledAt, lastInstallWarning;
    private static int installTicket;
    private static final long INSTALL_TIMEOUT_MS = 5000L;
    private static int policyRevision;
    private static int appliedRevision = -1;
    private static long lastRetryMs;
    private static final boolean[] missingSeedLogged = new boolean[12];

    private ScreenNavStatusGate() { }

    /** All reads/writes of CombiBAPListener.combiservice must happen on NavigationJobs. */
    private static void scheduleInstallLocked() {
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (installScheduled && (now < installScheduledAt || now - installScheduledAt >= INSTALL_TIMEOUT_MS)) {
            installScheduled = false;
            ++installTicket; // A late task may neither clear nor apply a newer task.
            appliedRevision = -1;
            installWarning("NavigationJobs task expired; scheduling replacement", null);
        }
        if (installScheduled || (appliedRevision == policyRevision
                && gate != null && !gate.hasPendingReplay())) return;
        Navigation navigation = Navigation.getInstance();
        if (navigation == null || navigation.getDispatcher() == null) return;
        installScheduled = true;
        installScheduledAt = now;
        final int ticket = ++installTicket;
        try {
            navigation.getDispatcher().execute(new Runnable() {
                public void run() { installNow(ticket); }
            });
        } catch (Throwable t) {
            installScheduled = false;
            installWarning("NavigationJobs submission failed", t);
        }
    }

    private static void installWarning(String message, Throwable failure) {
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (lastInstallWarning != 0L && now >= lastInstallWarning && now - lastInstallWarning < 10000L) return;
        lastInstallWarning = now;
        try { Log.w(TAG, message + (failure == null ? "" : ": " + failure)); } catch (Throwable ignored) { }
    }

    private static synchronized void installNow(int ticket) {
        if (!installScheduled || ticket != installTicket) return;
        installScheduled = false;
        try {
            Navigation navigation = Navigation.getInstance();
            if (navigation == null) return;
            ClusterService cs = navigation.getClusterService();
            if (cs == null) return;

            CombiBAPServiceNavi current = cs.getCombiBAPListenerCombiService();
            if (current == null) return;

            if (cs != clusterService || gate == null || current != gate) {
                GatedCombiService previous = gate;
                if (current instanceof GatedCombiService) {
                    gate = (GatedCombiService) current;
                } else {
                    gate = new GatedCombiService(current);
                }
                if (previous != null && previous != gate) previous.retire();
                gate.setRouteGuidanceBlocked(desiredRouteBlocked);
                gate.setCurrentPositionTextOnly(desiredCurrentPositionTextOnly);
                gate.setCurrentPositionInfoBlocked(desiredCurrentPositionBlocked);
                if (current != gate) cs.setCombiBAPListenerCombiService(gate);
                clusterService = cs;
                seedInstrumentState(cs);
                seedNativeData(cs);
                Log.i(TAG, "native route-guidance gate installed/reused");
            }

            gate.setRouteGuidanceBlocked(desiredRouteBlocked);
            gate.setCurrentPositionTextOnly(desiredCurrentPositionTextOnly);
            gate.setCurrentPositionInfoBlocked(desiredCurrentPositionBlocked);
            appliedRouteBlocked = desiredRouteBlocked;
            appliedRevision = gate.hasPendingReplay() ? -1 : policyRevision;
        } catch (Throwable t) {
            appliedRevision = -1;
            installWarning("navigation gate installation failed", t);
        }
    }

    /** Installing after stock updateAll must not wait for another Fct44/54 delta.
     * Read both accepted values on NavigationJobs, without sending synthetic BAP
     * responses. The fields can belong to the stock listener's superclass. */
    private static void seedNativeData(ClusterService cs) {
        // Replay the OEM listener's accepted values through its own update methods,
        // after the gate is attached, on NavigationJobs. Never fabricate BAP data.
        String[] methods = { "updateRGStatusAndType", "updateActiveRGType",
            "updateDistanceToNextManeuver", "updateCurrentPositionInfo",
            "updateManeuverDescriptor", "updateLaneGuidance", "updateExitView",
            "updateManeuverState", "updateTurnToInfo", "updateDistanceToDestination",
            "updateTimeToDestination", "updateDestinationInfo" };
        try {
            Object listener = cs.getClass().getMethod("getCombiBAPListener", new Class[0])
                .invoke(cs, new Object[0]);
            if (listener == null) return;
            for (int i=0; i<methods.length; i++) {
                Class type = listener.getClass();
                java.lang.reflect.Method method = null;
                while (type != null) {
                    try { method = type.getDeclaredMethod(methods[i], new Class[0]); break; }
                    catch (NoSuchMethodException absent) { type = type.getSuperclass(); }
                }
                if (method == null && !missingSeedLogged[i]) {
                    missingSeedLogged[i] = true;
                    Log.w(TAG, "OEM snapshot method unavailable class=" + listener.getClass().getName() + " method=" + methods[i]);
                }
                if (method != null) {
                    try { method.setAccessible(true); method.invoke(listener, new Object[0]); }
                    catch (Throwable t) { Log.w(TAG, "OEM snapshot seed failed method=" + methods[i] + ": " + t); }
                }
            }
        } catch (Throwable t) { Log.w(TAG, "OEM snapshot accessor unavailable: " + t); }
    }

    private static boolean readListenerBoolean(Object listener, String name) throws Exception {
        Class type = listener.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                return field.getBoolean(listener);
            } catch (NoSuchFieldException absent) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void seedInstrumentState(ClusterService cs) {
        /* Withdraw a previous service's visibility until the new snapshot is known. */
        GatedCombiService.publishMapVisibility(false);
        try {
            Object listener = cs.getClass().getMethod("getCombiBAPListener", new Class[0])
                .invoke(cs, new Object[0]);
            boolean visible = readListenerBoolean(listener, "supplementaryMapViewVisible");
            boolean largeMapView = readListenerBoolean(listener, "largeMapView");
            GatedCombiService.publishMapPresentation(largeMapView);
            GatedCombiService.publishMapVisibility(visible);
            Log.i(TAG, "cluster display-state seeded visible=" + visible
                + " largeMapView=" + largeMapView);
        } catch (Throwable t) {
            try { Log.w(TAG, "cluster display-state seed unavailable; awaiting stock Status: " + t); }
            catch (Throwable ignored) { }
        }
    }

    /** Called synchronously by ClusterService#setCombiBAPService on NavigationJobs, before the
     * stock CombiBAPListener setter performs updateAll().  Installing here closes the old race in
     * which updateAll painted native INITIALIZING/status values before the asynchronous wrapper
     * appeared, and avoids cross-thread writes to CombiBAPListener.combiservice. */
    public static synchronized CombiBAPServiceNavi prepareCombiBAPService(
            ClusterService cs, CombiBAPServiceNavi incoming) {
        if (incoming == null) {
            if (cs == clusterService && gate != null) {
                try { gate.retire(); } catch (Throwable t) { }
                clusterService = null;
                gate = null;
                appliedRouteBlocked = false;
                GatedCombiService.publishMapVisibility(false);
            }
            return null;
        }

        GatedCombiService next;
        if (incoming instanceof GatedCombiService) {
            next = (GatedCombiService) incoming;
        } else {
            next = new GatedCombiService(incoming);
        }
        if (gate != null && gate != next) {
            try { gate.retire(); } catch (Throwable t) { }
        }
        clusterService = cs;
        gate = next;
        gate.setRouteGuidanceBlocked(desiredRouteBlocked);
        gate.setCurrentPositionTextOnly(desiredCurrentPositionTextOnly);
        gate.setCurrentPositionInfoBlocked(desiredCurrentPositionBlocked);
        seedInstrumentState(cs);
        appliedRouteBlocked = desiredRouteBlocked;
        installScheduled = false;
        ++installTicket;
        Log.i(TAG, "native route-guidance gate installed on Navigation service edge");
        return gate;
    }

    /** Single owner of the native CombiBAP wrapper for both the cluster module and RGI. */
    public static synchronized boolean setRouteGuidanceBlocked(boolean blocked) {
        if (desiredRouteBlocked != blocked || (!blocked && desiredCurrentPositionBlocked)) ++policyRevision;
        desiredRouteBlocked = blocked;
        if (blocked && gate != null) gate.setRouteGuidanceBlocked(true);
        if (!blocked) {
            desiredCurrentPositionBlocked = false;
        }
        scheduleInstallLocked();
        return gate != null && appliedRouteBlocked == blocked && !installScheduled;
    }

    /** FctID 19/20/21/22/46 ownership follows actual CarPlay RGI, not the
     * session-long native-RG gate. The wrapper is already installed on
     * NavigationJobs, so changing these volatile policy bits requires no
     * service-field mutation. */
    public static synchronized void setCurrentPositionInfoBlocked(boolean blocked) {
        setCurrentPositionInfoBlocked(blocked, false);
    }

    /** textOnly (NO RGI): the block covers only FctID 19/20; stock keeps 21/22/46. */
    public static synchronized void setCurrentPositionInfoBlocked(boolean blocked, boolean textOnly) {
        if (desiredCurrentPositionBlocked != blocked
                || (blocked && desiredCurrentPositionTextOnly != textOnly)) ++policyRevision;
        desiredCurrentPositionBlocked = blocked;
        if (blocked) desiredCurrentPositionTextOnly = textOnly;
        if (blocked && gate != null) {
            gate.setCurrentPositionTextOnly(textOnly);
            gate.setCurrentPositionInfoBlocked(true);
        }
        scheduleInstallLocked();
    }

    /** Resident monitor retries dispatcher/service absence and failed replay,
     * including after the phone disconnects. OEM writes remain on NavigationJobs. */
    public static synchronized void retryPending() { retryPending(true); }
    public static synchronized void retryPending(boolean monitorServices) {
        if (!monitorServices && !desiredRouteBlocked && !desiredCurrentPositionBlocked
                && (gate == null || (appliedRevision == policyRevision && !gate.hasPendingReplay()))) return;
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (now >= lastRetryMs && now - lastRetryMs < 1000L) return;
        lastRetryMs = now;
        // OEM service identity can change independently of our policy revision.
        appliedRevision = -1;
        scheduleInstallLocked();
    }

    /** Exact stock-service availability edge, after prepareCombiBAPService + stock updateAll.
     * Also wakes CarPlayApp so RgdModule releases/reacquires its paired OSGi reference when the
     * Navigation service is replaced without a phone reconnect. */
    public static synchronized void onCombiBAPServiceChanged(ClusterService cs) {
        CombiBAPServiceNavi current = null;
        try { if (cs != null) current = cs.getCombiBAPListenerCombiService(); }
        catch (Throwable t) { }

        if (cs != clusterService || current != gate) {
            appliedRouteBlocked = false;
            appliedRevision = -1;
        }
        if (current == null) {
            if (cs == clusterService) {
                if (gate != null) gate.retire();
                clusterService = null;
                gate = null;
                GatedCombiService.publishMapVisibility(false);
            }
            /* Resident CarPlayHook retries unavailable navigation services. */
            return;
        }
        if (desiredRouteBlocked) scheduleInstallLocked();
        /* Resident CarPlayHook retries unavailable navigation services. */
    }
}
