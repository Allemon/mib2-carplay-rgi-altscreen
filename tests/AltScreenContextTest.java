import com.luka.carplay.cluster.AltScreenVideo;
import com.luka.carplay.core.ScreenModule;
import com.luka.carplay.framework.Log;
import de.audi.tghu.fwhmi.IDisplayManagerKombiControl;
import java.io.File;
import java.lang.reflect.*;
import java.util.*;

/** Real ScreenModule worker against a fake DisplayManager: AltScreen video markers select ctx 81. */
public final class AltScreenContextTest implements InvocationHandler {
    final List switches = Collections.synchronizedList(new ArrayList());
    volatile int current = 74;
    volatile int lastRate = -1;

    static void check(boolean b, String message) { if (!b) throw new AssertionError(message); }

    public Object invoke(Object proxy, Method method, Object[] args) {
        if (method.getName().equals("switchContext") && ((Integer)args[1]).intValue() == 1) {
            current = ((Integer)args[0]).intValue();
            switches.add(Integer.valueOf(current));
        }
        if (method.getName().equals("setUpdateRate") && ((Integer)args[0]).intValue() == 1)
            lastRate = ((Integer)args[1]).intValue();
        if (method.getName().equals("getCurrentContextID")) return Integer.valueOf(current);
        Class type = method.getReturnType();
        if (type == Boolean.TYPE) return Boolean.FALSE;
        if (type == Integer.TYPE) return Integer.valueOf(0);
        return null;
    }

    static void set(Class c, String name, Object value) throws Exception {
        Field f = c.getDeclaredField(name); f.setAccessible(true); f.set(null, value);
    }
    static Object call(String name) throws Exception {
        Method m = ScreenModule.class.getDeclaredMethod(name, new Class[0]);
        m.setAccessible(true); return m.invoke(null, new Object[0]);
    }
    static int contextFor(boolean connected, boolean video, boolean nav) throws Exception {
        Method m = ScreenModule.class.getDeclaredMethod("contextFor",
            new Class[]{Boolean.TYPE, Boolean.TYPE, Boolean.TYPE});
        m.setAccessible(true);
        return ((Integer)m.invoke(null, new Object[]{Boolean.valueOf(connected),
            Boolean.valueOf(video), Boolean.valueOf(nav)})).intValue();
    }

    void await(int ctx, String message) throws Exception {
        long end = System.currentTimeMillis() + 3000;
        while (System.currentTimeMillis() < end) {
            if (current == ctx) return;
            Thread.sleep(20);
        }
        throw new AssertionError(message + " (actual ctx " + current + ", switches " + switches + ")");
    }

    public static void main(String[] args) throws Exception {
        Log.setLevel(-1);
        check(contextFor(false, true, true) == 74, "disconnected is always stock");
        check(contextFor(true, false, false) == 74, "idle session stays stock");
        check(contextFor(true, false, true) == 80, "nav without video uses the stock map");
        check(contextFor(true, true, false) == 81, "video without nav uses ctx 81");
        check(contextFor(true, true, true) == 81, "video wins over the stock map during nav");

        File dir = new File(System.getProperty("java.io.tmpdir"), "altscreen-ctx-" + System.nanoTime());
        check(dir.mkdirs(), "scratch dir");
        File active = new File(dir, "active"), ready = new File(dir, "ready");
        set(AltScreenVideo.class, "activePath", active.getPath());
        set(AltScreenVideo.class, "readyPath", ready.getPath());

        // GEM "cluster refresh" choice: ctx 81 only, 10..60, anything else -> 30.
        File fps = new File(dir, "cluster_fps");
        set(ScreenModule.class, "videoFpsPath", fps.getPath());
        Method videoFps = ScreenModule.class.getDeclaredMethod("videoClusterFps", new Class[0]);
        videoFps.setAccessible(true);
        check(((Integer)videoFps.invoke(null, new Object[0])).intValue() == 30, "no choice -> 30");
        String[][] cases = { {"60\n", "60"}, {"5", "30"}, {"abc", "30"}, {"99", "30"}, {" 45 ", "45"} };
        for (int i = 0; i < cases.length; i++) {
            java.io.FileOutputStream o = new java.io.FileOutputStream(fps);
            o.write(cases[i][0].getBytes()); o.close();
            check(((Integer)videoFps.invoke(null, new Object[0])).intValue() == Integer.parseInt(cases[i][1]),
                "fps file '" + cases[i][0].trim() + "' -> " + cases[i][1]);
        }
        java.io.FileOutputStream o60 = new java.io.FileOutputStream(fps);
        o60.write("60".getBytes()); o60.close();

        AltScreenContextTest capture = new AltScreenContextTest();
        Object dm = Proxy.newProxyInstance(AltScreenContextTest.class.getClassLoader(),
            new Class[]{IDisplayManagerKombiControl.class}, capture);
        set(ScreenModule.class, "platformSupported", Boolean.TRUE);
        Field dmField = ScreenModule.class.getDeclaredField("dm");
        dmField.setAccessible(true);
        final ScreenModule module = new ScreenModule();
        dmField.set(module, dm);
        set(ScreenModule.class, "connected", Boolean.TRUE);
        final Method loop = ScreenModule.class.getDeclaredMethod("switchLoop", new Class[0]);
        loop.setAccessible(true);
        Thread worker = new Thread(new Runnable() { public void run() {
            try { loop.invoke(module, new Object[0]); } catch (Throwable t) { t.printStackTrace(); }
        }});
        worker.setDaemon(true);
        worker.start();
        capture.await(74, "session starts on stock");

        // Demand alone is not enough: the sidecar has not presented a frame yet.
        check(active.createNewFile(), "active marker");
        Thread.sleep(600);
        check(capture.current == 74 && !ScreenModule.isAltScreenVideo(), "demand without ready stays stock");
        check(ready.createNewFile(), "ready marker");
        capture.await(81, "ready video enters ctx 81");
        Thread.sleep(50);
        check(capture.lastRate == 60, "ctx 81 uses the chosen encoder rate: " + capture.lastRate);
        check(ScreenModule.isAltScreenVideo(), "video flag published");
        int i81 = capture.switches.lastIndexOf(Integer.valueOf(81));
        check(i81 > 0 && ((Integer)capture.switches.get(i81 - 1)).intValue() == 72,
            "ctx 81 is entered through the 72 bounce: " + capture.switches);

        // Route guidance during video keeps the video context; video loss falls back to the map.
        ScreenModule.setNavActive(true);
        Thread.sleep(400);
        check(capture.current == 81, "nav does not leave the video context");
        check(ready.delete(), "drop ready");
        capture.await(80, "video loss during nav falls back to stock map ctx 80");
        Thread.sleep(50);
        check(capture.lastRate == 30, "ctx 80 keeps the tested 30 fps: " + capture.lastRate);
        ScreenModule.setNavActive(false);
        capture.await(74, "route end without KDK visibility returns to stock");

        // Stock drift while owning ctx 81 is reconciled back.
        check(ready.createNewFile(), "ready again");
        capture.await(81, "video returns");
        capture.current = 74;
        capture.await(81, "drift is reconciled to ctx 81");

        // Disconnect releases the cluster even with stale markers on disk.
        set(ScreenModule.class, "connected", Boolean.FALSE);
        call("republish");
        capture.await(74, "disconnect restores stock");
        Thread.sleep(400);
        check(capture.current == 74 && !ScreenModule.isAltScreenVideo(), "stale markers ignored while disconnected");

        fps.delete(); active.delete(); ready.delete(); dir.delete();
        System.out.println("AltScreenContextTest: marker gating, 72 bounce, nav/video priority, video loss, drift, disconnect, ctx 81 encoder rate PASS");
    }
}
