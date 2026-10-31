package com.luka.carplay.routeguidance;

import java.lang.reflect.Method;
import com.luka.carplay.framework.Log;

/* Commit field ownership only after the OEM setter accepts the value. Failed
 * updates/clears stay pending and are retried by the resident service probe. */
final class LowerBarKomo {
    private boolean roadOwned, distanceOwned, arrivalOwned, clearing;
    private RouteGuidance.State cached;
    private int pendingMask;
    private Object owner;
    private LowerBarKomo pendingNext;
    private boolean cleanupQueued;
    private static final Object CLEANUP_LOCK = new Object();
    private static LowerBarKomo pendingCleanup;
    /* Field ownership survives object retirement, but an old failed clear may
     * never erase fields accepted from a new RGI instance on the same service. */
    private static final class TargetGate {
        Object target; int users; TargetGate next;
        LowerBarKomo road, distance, arrival;
    }
    private static TargetGate targets;
    private static TargetGate acquireTarget(Object target) {
        synchronized (CLEANUP_LOCK) {
            TargetGate t = targets;
            while (t != null && t.target != target) t = t.next;
            if (t == null) { t = new TargetGate(); t.target = target; t.next = targets; targets = t; }
            ++t.users; return t;
        }
    }
    private static void releaseTarget(TargetGate target) {
        synchronized (CLEANUP_LOCK) {
            if (--target.users == 0 && target.road == null && target.distance == null && target.arrival == null) {
                TargetGate t = targets, previous = null;
                while (t != null && t != target) { previous = t; t = t.next; }
                if (t != null) { if (previous == null) targets = t.next; else previous.next = t.next; }
            }
        }
    }
    private long lastWarning, lastRetry;
    private final boolean[] missingLogged = new boolean[4];

    synchronized void update(Object cluster, RouteGuidance.State s) {
        if (cluster == null) return;
        if (owner != null && owner != cluster) clearForRebind(owner);
        owner = cluster;
        clearing = false;
        if (cached == null) cached = new RouteGuidance.State();
        int dirty = s.dirtyMask;
        if ((dirty & RouteGuidance.State.DIRTY_CURRENT_ROAD) != 0) cached.currentRoad = s.currentRoad;
        if ((dirty & RouteGuidance.State.DIRTY_DIST_DEST) != 0) cached.distDestM = s.distDestM;
        if ((dirty & RouteGuidance.State.DIRTY_ETA) != 0) cached.etaSeconds = s.etaSeconds;
        cached.dirtyMask |= dirty & fields();
        pendingMask |= dirty & fields();
        apply(cluster);
    }
    private static int fields() {
        return RouteGuidance.State.DIRTY_CURRENT_ROAD | RouteGuidance.State.DIRTY_DIST_DEST | RouteGuidance.State.DIRTY_ETA;
    }
    private void apply(Object cluster) {
        if (cluster == null || cached == null) return;
        TargetGate target = acquireTarget(cluster);
        try { synchronized (target) { applyFields(cluster, target); } }
        finally { releaseTarget(target); }
    }
    private void applyFields(Object cluster, TargetGate target) {
        if (clearing) {
            roadOwned = roadOwned && target.road == this;
            distanceOwned = distanceOwned && target.distance == this;
            arrivalOwned = arrivalOwned && target.arrival == this;
        }
        if ((pendingMask & RouteGuidance.State.DIRTY_CURRENT_ROAD) != 0) {
            boolean valid = cached.currentRoad != null && cached.currentRoad.trim().length() > 0;
            if ((!valid && !roadOwned) || call(cluster, "updateCurrentStreet", 0,
                    new Class[]{String.class}, new Object[]{valid ? cached.currentRoad : ""})) {
                roadOwned = valid; if (valid) target.road = this; else if (target.road == this) target.road = null; pendingMask &= ~RouteGuidance.State.DIRTY_CURRENT_ROAD;
            }
        }
        if ((pendingMask & RouteGuidance.State.DIRTY_DIST_DEST) != 0) {
            boolean valid = cached.distDestM >= 0;
            if (!valid && !distanceOwned) pendingMask &= ~RouteGuidance.State.DIRTY_DIST_DEST;
            else if (call(cluster, "updateDistanceToDestination", 1,
                    new Class[]{Integer.TYPE, Boolean.TYPE}, new Object[]{new Integer(valid ? cached.distDestM : 0), Boolean.FALSE})) {
                if (valid) { distanceOwned = true; target.distance = this; } // Setter accepted before flush.
                if (flush(cluster)) { distanceOwned = valid; if (!valid && target.distance == this) target.distance = null; pendingMask &= ~RouteGuidance.State.DIRTY_DIST_DEST; }
            }
        }
        if ((pendingMask & RouteGuidance.State.DIRTY_ETA) != 0) {
            boolean valid = cached.etaSeconds >= 0;
            if (!valid && !arrivalOwned) pendingMask &= ~RouteGuidance.State.DIRTY_ETA;
            else if (call(cluster, "updateArrivalTime", 2,
                    new Class[]{Boolean.TYPE, Long.TYPE, Boolean.TYPE}, new Object[]{valid ? Boolean.TRUE : Boolean.FALSE,
                        new Long(valid ? cached.etaSeconds * 1000L : 0L), Boolean.FALSE})) {
                if (valid) { arrivalOwned = true; target.arrival = this; }
                if (flush(cluster)) { arrivalOwned = valid; if (!valid && target.arrival == this) target.arrival = null; pendingMask &= ~RouteGuidance.State.DIRTY_ETA; }
            }
        }
        if (clearing && pendingMask == 0) { cached = null; owner = null; clearing = false; }
    }
    synchronized void clear(Object cluster) {
        clearing = true;
        cached = new RouteGuidance.State();
        cached.dirtyMask = fields(); pendingMask = fields();
        if (owner == null) owner = cluster;
        apply(owner);
        queueCleanupIfNeeded();
    }
    synchronized void clearForRebind(Object cluster) {
        if (roadOwned || distanceOwned || arrivalOwned) {
            LowerBarKomo previous = new LowerBarKomo();
            previous.roadOwned = roadOwned; previous.distanceOwned = distanceOwned; previous.arrivalOwned = arrivalOwned;
            previous.owner = owner != null ? owner : cluster;
            TargetGate target = acquireTarget(previous.owner);
            try { synchronized (target) {
                if (target.road == this) target.road = previous;
                if (target.distance == this) target.distance = previous;
                if (target.arrival == this) target.arrival = previous;
            } } finally { releaseTarget(target); }
            previous.clear(previous.owner);
        }
        roadOwned = distanceOwned = arrivalOwned = false;
        owner = null; pendingMask = 0; clearing = false;
    }
    synchronized void retry(Object cluster) {
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (lastRetry != 0L && now >= lastRetry && now - lastRetry < 2000L) return;
        lastRetry = now;
        if (pendingMask != 0) apply(owner != null ? owner : cluster);
        queueCleanupIfNeeded();
    }
    private void queueCleanupIfNeeded() {
        synchronized (CLEANUP_LOCK) {
            if (clearing && pendingMask != 0 && !cleanupQueued) {
                cleanupQueued = true; pendingNext = pendingCleanup; pendingCleanup = this;
            } else if ((!clearing || pendingMask == 0) && cleanupQueued) {
                LowerBarKomo p = pendingCleanup, previous = null;
                while (p != null && p != this) { previous = p; p = p.pendingNext; }
                if (p != null) { if (previous == null) pendingCleanup = pendingNext; else previous.pendingNext = pendingNext; }
                cleanupQueued = false; pendingNext = null;
            }
        }
    }
    static void retryCleanup() {
        LowerBarKomo[] snapshot;
        synchronized (CLEANUP_LOCK) {
            int count = 0; LowerBarKomo p;
            for (p = pendingCleanup; p != null; p = p.pendingNext) ++count;
            snapshot = new LowerBarKomo[count]; count = 0;
            for (p = pendingCleanup; p != null; p = p.pendingNext) snapshot[count++] = p;
        }
        // No queue lock while acquiring a field owner's monitor or calling OEM.
        for (int i=0; i<snapshot.length; ++i) snapshot[i].retry(null);
    }
    synchronized void replay(Object cluster) {
        if (cached != null && !clearing) { pendingMask |= cached.dirtyMask & fields(); owner = cluster; apply(cluster); }
    }
    private boolean flush(Object cluster) { return call(cluster, "updateKOMOFollowInfo", 3, new Class[0], new Object[0]); }
    private boolean call(Object target, String name, int index, Class[] signature, Object[] args) {
        try {
            Class type = target.getClass(); Method method = null;
            while (type != null) {
                try { method = type.getDeclaredMethod(name, signature); break; }
                catch (NoSuchMethodException e) { type = type.getSuperclass(); }
            }
            if (method == null) {
                if (!missingLogged[index]) { missingLogged[index] = true; warn(name + " unavailable on " + target.getClass().getName()); }
                return false;
            }
            method.setAccessible(true); method.invoke(target, args); return true;
        } catch (Throwable t) { warn(name + " failed: " + t); return false; }
    }
    private void warn(String message) {
        long now = com.luka.carplay.framework.ElapsedClock.nowMs();
        if (lastWarning != 0L && now >= lastWarning && now - lastWarning < 10000L) return;
        lastWarning = now;
        try { Log.w("LowerBarKomo", message); } catch (Throwable ignored) { }
    }
}
