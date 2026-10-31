/*
 * TelephonyEventBridge - CarPlay call state and dialing for GatedPhoneService through
 * the stock terminal-mode event bus.
 *
 * Stock DSICarplayListenerImpl.updateCallState turns every DSICarplay call-state update
 * into IEventBus.updateCallState(CallStateChangedEvent list), and the bus delivers it on
 * the terminal-mode dispatcher to every registered IEventListener.  IEventBus.callNumber
 * reaches the stock CarplayDSIController, which dials with requestUI2("tel:<number>").
 * Listening on the bus replaces LuKa's patched CarplayDSILifecycleController: that class
 * differs between the US/EU and CN trains, while IEventBus, IEventListener,
 * DefaultEventListener and CallStateChangedEvent are identical on MU1316, MU1329 and
 * MU1003, so one jar serves all of them.
 *
 * Java 1.2: no generics; GList is read through its erased interface.
 */
package com.luka.carplay.telephony;

import com.luka.carplay.framework.Log;
import de.audi.app.terminalmode.IContext;
import de.audi.app.terminalmode.events.CallStateChangedEvent;
import de.audi.app.terminalmode.events.DefaultEventListener;
import de.audi.app.terminalmode.events.IEventBus;
import de.audi.atip.utils.generics.GList;

public final class TelephonyEventBridge extends DefaultEventListener {
    private static final String TAG = "Telephony";

    private static IEventBus bus;
    private static TelephonyEventBridge listener;

    private TelephonyEventBridge() {}

    /** From CarPlayHook on every CarPlay activation; registers once per event bus. */
    public static void attach(Object context) {
        try {
            if (!(context instanceof IContext)) return;
            final IEventBus b = ((IContext)context).getEventBus();
            if (b == null) return;
            synchronized (TelephonyEventBridge.class) {
                if (b == bus) return;
                TelephonyEventBridge l = new TelephonyEventBridge();
                if (!b.registerListener(l)) {
                    Log.w(TAG, "event bus refused the call state listener");
                    return;
                }
                if (bus != null && listener != null) {
                    try { bus.unregisterListener(listener); } catch (Throwable ignored) { }
                }
                bus = b;
                listener = l;
            }
            GatedPhoneService.setCommands(new GatedPhoneService.Commands() {
                public void dial(String number) { b.callNumber(number); }
            });
            Log.w(TAG, "TELEPHONY_BRIDGE call state and dial on the terminal-mode event bus");
        } catch (Throwable t) {
            Log.w(TAG, "event bus bridge unavailable: " + t);
        }
    }

    public void updateCallState(GList list) {
        try {
            int n = list != null ? list.size() : 0;
            CallStateChangedEvent[] calls = new CallStateChangedEvent[n];
            for (int i = 0; i < n; i++) {
                Object o = list.get(i);
                calls[i] = o instanceof CallStateChangedEvent ? (CallStateChangedEvent)o : null;
            }
            GatedPhoneService.onCarPlayCalls(calls);
        } catch (Throwable t) {
            Log.w(TAG, "call state update dropped: " + t);
        }
    }
}
