package com.luka.carplay.routeguidance;

import de.audi.tghu.navi.app.command.DSIResponseContainer;

/** Keep native DSI state separate from the temporary CarPlay acceptance flag.
 * The OEM setter is synchronized on its container. The build patch calls
 * nativeWrite inside that same monitor, so release cannot miss a DSI update. */
public final class NativeRouteStateOverlay {
    private static final class State {
        DSIResponseContainer container;
        boolean latest;
        boolean ownWrite;
    }
    private static volatile State current;

    private NativeRouteStateOverlay() { }

    public static boolean nativeWrite(Object container, boolean value) {
        State state = current;
        if (state == null || state.container != container || state.ownWrite) return value;
        state.latest = value;
        return true;
    }

    public static void apply(DSIResponseContainer container, boolean active) {
        if (container == null) return;
        State previous = current;
        if (active && previous != null && previous.container != container) releaseCurrent();
        synchronized (container) {
            State state = current;
            if (active) {
                if (state != null && state.container != container)
                    throw new IllegalStateException("another native container still owns the overlay");
                if (state == null) {
                    state = new State();
                    state.container = container;
                    state.latest = container.isRgActive();
                    current = state;
                }
                state.ownWrite = true;
                try { container.setRgActive(true); }
                finally { state.ownWrite = false; }
            } else if (state != null && state.container == container) {
                current = null;
                container.setRgActive(state.latest);
            }
        }
    }

    /** Release by our retained container, even if an old ClusterService can
     * no longer return it. A failed OEM restore must not poison future overlays. */
    public static void releaseCurrent() {
        State state = current;
        if (state == null) return;
        synchronized (state.container) {
            if (current != state) return;
            current = null;
            state.container.setRgActive(state.latest);
        }
    }
}
