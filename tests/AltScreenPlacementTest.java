import com.luka.carplay.cluster.AltScreenPlacement;
import de.audi.atip.hmi.view.IDisplayManager;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.*;
import java.util.*;

/** AltScreenPlacement against a fake DisplayManager: displayable 3 follows the stock map rule. */
public final class AltScreenPlacementTest implements InvocationHandler {
    final List moves = new ArrayList();

    static void check(boolean b, String message) { if (!b) throw new AssertionError(message); }

    public Object invoke(Object proxy, Method method, Object[] args) {
        if (method.getName().equals("setPosition")) {
            moves.add(args[0] + "@" + args[2] + "," + args[3]);
        }
        Class type = method.getReturnType();
        if (type == Boolean.TYPE) return Boolean.FALSE;
        if (type == Integer.TYPE) return Integer.valueOf(0);
        return null;
    }

    static void reset(String path) throws Exception {
        Method m = AltScreenPlacement.class.getDeclaredMethod("resetForTest", new Class[]{String.class});
        m.setAccessible(true);
        m.invoke(null, new Object[]{path});
    }

    static void write(File f, String text) throws Exception {
        FileOutputStream out = new FileOutputStream(f);
        out.write(text.getBytes("US-ASCII"));
        out.close();
    }

    String last() { return moves.isEmpty() ? "none" : (String)moves.get(moves.size() - 1); }

    public static void main(String[] a) throws Exception {
        File cfg = File.createTempFile("cluster_shift", ".cfg");
        cfg.delete();
        AltScreenPlacementTest t = new AltScreenPlacementTest();
        IDisplayManager dm = (IDisplayManager)Proxy.newProxyInstance(
            IDisplayManager.class.getClassLoader(), new Class[]{IDisplayManager.class}, t);
        String classic = "LayoutMIB2HighB9", sport = "LayoutMIB2HighB9Sport";

        /* Classic: no small-stage offset, no config -> displayable 3 is never written. */
        reset(cfg.getPath());
        AltScreenPlacement.onMapPlacement(dm, 1, classic, 0, 26, 0, 0, false);
        AltScreenPlacement.onMapPlacement(dm, 1, classic, 0, 26, 0, 0, true);
        AltScreenPlacement.poll();
        AltScreenPlacement.onVideoReady();
        check(t.moves.isEmpty(), "classic without shift must not touch displayable 3: " + t.moves);

        /* Sport: small view moves the plane like stock moves 33/58, full view brings it back. */
        AltScreenPlacement.onMapPlacement(dm, 1, sport, 0, 26, -476, 0, true);
        check(t.last().equals("3@-476,26"), "sport small: " + t.moves);
        AltScreenPlacement.onMapPlacement(dm, 1, sport, 0, 26, -476, 0, true);
        check(t.moves.size() == 1, "same position written twice: " + t.moves);
        AltScreenPlacement.onVideoReady();
        check(t.moves.size() == 2 && t.last().equals("3@-476,26"), "video ready rewrites: " + t.moves);
        AltScreenPlacement.onMapPlacement(dm, 1, sport, 0, 26, -476, 0, false);
        check(t.last().equals("3@0,26"), "sport full returns to origin: " + t.moves);

        /* GEM shift: picked up live by poll(); removing the file restores the origin. */
        reset(cfg.getPath());
        t.moves.clear();
        AltScreenPlacement.onMapPlacement(dm, 1, classic, 0, 26, 0, 0, false);
        write(cfg, "full_dx=-120\nsmall_dx=-476\n");
        AltScreenPlacement.poll();
        check(t.last().equals("3@-120,26"), "full_dx applied: " + t.moves);
        AltScreenPlacement.onMapPlacement(dm, 1, classic, 0, 26, 0, 0, true);
        check(t.last().equals("3@-476,26"), "small_dx simulates Sport on Classic: " + t.moves);
        cfg.delete();
        AltScreenPlacement.poll();
        check(t.last().equals("3@0,26"), "config removed -> origin: " + t.moves);

        /* Out-of-range or malformed values are ignored. */
        write(cfg, "full_dx=-9000\nsmall_dx=abc\n");
        int before = t.moves.size();
        AltScreenPlacement.poll();
        check(t.moves.size() == before, "bad values must not move the plane: " + t.moves);
        cfg.delete();

        System.out.println("AltScreenPlacementTest: PASS (classic untouched, Sport small-stage -476, GEM shift live, bad config ignored)");
    }
}
