/*
 * StockPhoneKeyFilter - drop the duplicate stock MFL phone key during a gated CarPlay call,
 * at runtime, without shipping any stock class.
 *
 * One Tel press reaches CarPlay twice: the VC sends AcceptCall/HangupCall over BAP, which
 * GatedPhoneListener turns into iAP2, and the stock MFLPhoneButtonsListener posts the HID
 * telephony key (FLASH 20 on a one-key wheel, HOOK 13 / HANGUP 14 on a two-key wheel).  On a
 * unit where the HID key works it acts a second time right after the iAP2 accept and ends
 * the call.  LuKa drops the stock copy in a patched
 * CarplayDSIController.postDSICarplayButtonEvent; this project ships one jar for every
 * train, so the same cut is made at runtime instead.
 *
 * Stock path, byte-identical on MU1316 and the CN MU1003 train:
 *   MFLPhoneButtonsListener$1/$2 -> DSISmartphoneManagerProxy$PhoneCallControllerProxy
 *     -> DSISmartphoneManagerProxy.phoneCallController   (volatile, read on every press)
 *     -> CarPlayDSIManager$1.hook/hangup/flash -> postDSICarplayButtonEvent(13/14/20)
 * postDSICarplayButtonEvent has no other caller, so a java.lang.reflect.Proxy in that
 * volatile field drops exactly what LuKa's patch drops; every other phone key, and the stock
 * endCall/postButtonEvent paths, stay untouched.  The field is not final, and the manager
 * is reached through the public IContext.getSmartphoneDSIManager().
 *
 * DSISmartphoneManagerProxy$ActiveDeviceListener rewrites the field on every active-device
 * update, so the filter is re-armed on activation and on every CarPlay call-state update
 * that carries a call, i.e. while the phone rings and before the Tel press.
 *
 * Any failure leaves stock untouched (the double answer persists, CarPlay is unaffected).
 * The reason is logged once at WARN, the only level the vehicle log keeps.
 *
 * Java 1.2 source level apart from Proxy (1.3, firmware jcl).
 *
 * Copyright (c) 2026.  GPL-3.0 (same as the rest of hmi/).
 */
package com.luka.carplay.telephony;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Proxy;

import com.luka.carplay.framework.Log;

import de.audi.app.terminalmode.IContext;

public final class StockPhoneKeyFilter {

    private static final String TAG = "Telephony";
    private static final String MARK = "TELEPHONY_STOCK_HID_FILTER ";

    /* DSISmartphoneManagerProxy's per-device phone controller and its type. */
    private static final String FIELD = "phoneCallController";
    private static final String CONTROLLER = "de.audi.app.terminalmode.smartphone.IPhoneCallController";
    /* CarPlayDSIManager$1 lives here; Android Auto / CarLife controllers are left alone. */
    private static final String CARPLAY_PACKAGE = "de.audi.app.terminalmode.smartphone.carplay.";

    /* DSICarplay BUTTON_* that CarPlayDSIManager$1 posts for each IPhoneCallController call. */
    private static final int HOOK = 13, HANGUP = 14, FLASH = 20;

    private static Object context;     /* IContext of the active CarPlay session */
    private static Object manager;     /* the stock DSISmartphoneManagerProxy we patched */
    private static Object original;    /* the stock CarPlay controller we wrapped */
    private static Object installed;   /* our proxy around it */
    private static String lastNote;    /* last failure reason, logged once */

    private StockPhoneKeyFilter() {}

    /** From CarPlayHook on every CarPlay activation. Never throws into the activation. */
    public static synchronized void install(Object ctx) {
        context = ctx;
        lastNote = null;
        arm("activate");
    }

    /** From GatedPhoneService on a CarPlay call-state update that carries a call. */
    public static synchronized void ensure() {
        if (context != null) arm("call");
    }

    /** From CarPlayHook on deactivate: put the stock controller back if ours is still set. */
    public static synchronized void uninstall() {
        try {
            if (manager != null && installed != null) {
                Field f = findField(manager.getClass(), FIELD);
                if (f != null) {
                    f.setAccessible(true);
                    if (f.get(manager) == installed) f.set(manager, original);
                }
            }
        } catch (Throwable t) {
            Log.w(TAG, MARK + "not restored: " + t);
        } finally {
            context = manager = original = installed = null;
            lastNote = null;
        }
    }

    private static void arm(String why) {
        try {
            if (!(context instanceof IContext)) {
                note(why, "context is " + name(context));
                return;
            }
            Object mgr = ((IContext) context).getSmartphoneDSIManager();
            if (mgr == null) {
                note(why, "no smartphone manager");
                return;
            }
            Field f = findField(mgr.getClass(), FIELD);
            if (f == null || !CONTROLLER.equals(f.getType().getName())) {
                note(why, "no " + FIELD + " on " + name(mgr));
                return;
            }
            if (Modifier.isFinal(f.getModifiers())) {
                note(why, FIELD + " is final on " + name(mgr));
                return;
            }
            f.setAccessible(true);
            Object cur = f.get(mgr);
            if (cur == null) {
                note(why, "no phone controller yet");
                return;
            }
            if (cur == installed) return;                     /* still in place */
            Object real = unwrap(cur);
            if (real != cur) {                                /* ours, from an earlier session */
                manager = mgr;
                original = real;
                installed = cur;
                lastNote = null;
                Log.w(TAG, MARK + "re-attached (" + why + "): " + name(mgr) + "." + FIELD);
                return;
            }
            if (!name(cur).startsWith(CARPLAY_PACKAGE)) {
                note(why, "active phone controller is " + name(cur));
                return;
            }
            /* Stock wrote the same CarPlay controller back: re-use our proxy. */
            boolean rearm = installed != null && mgr == manager && cur == original;
            Object proxy = rearm ? installed
                : Proxy.newProxyInstance(f.getType().getClassLoader(), new Class[] { f.getType() },
                                         new Handler(cur));
            f.set(mgr, proxy);
            if (f.get(mgr) != proxy) {
                note(why, "field write did not take");
                return;
            }
            manager = mgr;
            original = cur;
            installed = proxy;
            lastNote = null;
            Log.w(TAG, MARK + (rearm ? "re-armed" : "installed") + " (" + why + "): " + name(mgr) + "."
                + FIELD + " -> " + name(cur) + ", MFL hook/hangup/flash dropped during a gated CarPlay call");
        } catch (Throwable t) {
            note(why, "error " + t);
        }
    }

    /** WARN once per distinct reason; repeated call-state updates must not flood the log. */
    private static void note(String why, String reason) {
        if (reason.equals(lastNote)) return;
        lastNote = reason;
        Log.w(TAG, MARK + "not installed (" + why + "): " + reason);
    }

    private static Object unwrap(Object o) {
        try {
            if (Proxy.isProxyClass(o.getClass())) {
                InvocationHandler h = Proxy.getInvocationHandler(o);
                if (h instanceof Handler) return ((Handler) h).real;
            }
        } catch (Throwable t) {
            /* not ours */
        }
        return o;
    }

    private static Field findField(Class c, String name) {
        while (c != null && c != Object.class) {
            try { return c.getDeclaredField(name); }
            catch (NoSuchFieldException e) { c = c.getSuperclass(); }
        }
        return null;
    }

    private static String name(Object o) {
        return o == null ? "null" : o.getClass().getName();
    }

    /** Forwards every IPhoneCallController call to the stock CarPlay controller, except
     * hook/hangup/flash while GatedPhoneService has a gated CarPlay call. */
    private static final class Handler implements InvocationHandler {
        final Object real;

        Handler(Object real) { this.real = real; }

        public Object invoke(Object proxy, Method m, Object[] args) throws Throwable {
            String n = m.getName();
            if (m.getDeclaringClass() == Object.class) {
                if ("equals".equals(n)) return args[0] == proxy ? Boolean.TRUE : Boolean.FALSE;
                if ("hashCode".equals(n)) return new Integer(System.identityHashCode(proxy));
                if ("toString".equals(n)) return "StockPhoneKeyFilter[" + real + "]";
            }
            int id = "hook".equals(n) ? HOOK : "hangup".equals(n) ? HANGUP : "flash".equals(n) ? FLASH : -1;
            if (id > 0 && args != null && args.length == 1 && GatedPhoneService.dropStockPhoneKey(id)) {
                Log.w(TAG, "TELEPHONY_STOCK_HID_DROPPED " + n + "(" + args[0] + ")");
                return null;                                  /* the VC's iAP2 copy acts */
            }
            try {
                return m.invoke(real, args);
            } catch (InvocationTargetException e) {
                throw e.getTargetException();
            }
        }
    }
}
