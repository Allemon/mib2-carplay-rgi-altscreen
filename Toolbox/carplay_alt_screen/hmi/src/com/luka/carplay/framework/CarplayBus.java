/*
 * CarPlay Hook Bus - Java server
 *
 * One-way TCP event stream from libcarplay_hook.so to Java.
 * Java is the long-lived server on 127.0.0.1:19810; the hook is the
 * short-lived client inside dio_manager.  Touchpad input stays entirely
 * in the Java/DSI path and does not use this socket.
 *
 * Java 1.2 compatible: no generics, no lambdas, no try-with-resources.
 */

package com.luka.carplay.framework;

import java.io.DataInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;

public class CarplayBus {

    private static final String TAG = "CarplayBus";

    public static final String HOST = "127.0.0.1";
    public static final int    PORT = 19810;
    public static final int    MAGIC = 0x43504842;
    public static final int    HEADER_SIZE = 16;
    public static final int    MAX_PAYLOAD = 128 * 1024;

    public static final int FLAG_STICKY  = 0x01;
    public static final int FLAG_BINARY  = 0x02;
    public static final int FLAG_REPLAY  = 0x04;

    public static final int EVT_HELLO       = 0x0001;
    public static final int EVT_SYNC_BEGIN  = 0x0002;
    public static final int EVT_SYNC_END    = 0x0003;
    public static final int EVT_PONG        = 0x0004;  /* legacy/debug only */
    public static final int EVT_COVERART    = 0x0010;
    public static final int EVT_RGD_UPDATE  = 0x0020;
    public static final int EVT_DEVICE_STATE= 0x0030;

    /* Legacy constants kept so old debug callers still compile. */
    public static final int CMD_SYNC_REQ    = 0x0100;
    public static final int CMD_PING        = 0x0101;

    public interface Listener {
        void onFrame(int type, int flags, byte[] payload, int len);
    }

    public static class Data {
        private static final int MAX = 512;
        private String[] keys = new String[MAX];
        private String[] vals = new String[MAX];
        private int count = 0;

        void put(String key, String value) {
            if (count < MAX) {
                keys[count] = key;
                vals[count] = value;
                count++;
            }
        }

        public String str(String key, String def) {
            for (int i = 0; i < count; i++) {
                if (key.equals(keys[i])) return vals[i];
            }
            return def;
        }

        public String str(String key) { return str(key, null); }

        public int num(String key, int def) {
            String v = str(key, null);
            if (v == null) return def;
            try { return Integer.parseInt(v.trim()); } catch (Exception e) { return def; }
        }

        public long num64(String key, long def) {
            String v = str(key, null);
            if (v == null) return def;
            try { return Long.parseLong(v.trim()); } catch (Exception e) { return def; }
        }

        public boolean bool(String key, boolean def) {
            String v = str(key, null);
            if (v == null) return def;
            v = v.trim();
            if ("true".equals(v) || "1".equals(v)) return true;
            if ("false".equals(v) || "0".equals(v)) return false;
            return def;
        }

        public int[] intList(String key) {
            String v = str(key, null);
            if (v == null || v.length() == 0) return null;
            int commas = 0;
            for (int i = 0; i < v.length(); i++) if (v.charAt(i) == ',') commas++;
            int[] out = new int[commas + 1];
            int start = 0, idx = 0;
            for (int i = 0; i <= v.length(); i++) {
                if (i == v.length() || v.charAt(i) == ',') {
                    try { out[idx++] = Integer.parseInt(v.substring(start, i).trim()); }
                    catch (Exception e) { out[idx++] = 0; }
                    start = i + 1;
                }
            }
            return out;
        }

        public String[] strList(String key, char delim) {
            String v = str(key, null);
            if (v == null || v.length() == 0) return null;
            int c = 1;
            for (int i = 0; i < v.length(); i++) if (v.charAt(i) == delim) c++;
            String[] out = new String[c];
            int start = 0, idx = 0;
            for (int i = 0; i <= v.length(); i++) {
                if (i == v.length() || v.charAt(i) == delim) {
                    out[idx++] = v.substring(start, i);
                    start = i + 1;
                }
            }
            return out;
        }

        public boolean has(String key) {
            for (int i = 0; i < count; i++) if (key.equals(keys[i])) return true;
            return false;
        }

        public int size() { return count; }
    }

    public static Data parseText(byte[] buf, int len) {
        Data d = new Data();
        if (buf == null || len <= 0) return d;
        String content;
        try { content = new String(buf, 0, len, "UTF-8"); }
        catch (Exception e) { content = new String(buf, 0, len); }
        int pos = 0;
        while (pos < content.length()) {
            int eol = content.indexOf('\n', pos);
            if (eol < 0) eol = content.length();
            String line = content.substring(pos, eol);
            pos = eol + 1;
            if (line.length() == 0 || line.charAt(0) == '@') continue;
            int c1 = line.indexOf(':');
            if (c1 < 0) continue;
            int c2 = line.indexOf(':', c1 + 1);
            String key = line.substring(0, c1);
            String val = (c2 >= 0 && c2 + 1 <= line.length()) ? line.substring(c2 + 1) : "";
            d.put(key, val);
        }
        return d;
    }

    private static final CarplayBus INSTANCE=new CarplayBus();
    public static CarplayBus getInstance() { return INSTANCE; }
    private CarplayBus() { }
    private com.luka.carplay.bus.CarplayBus bus() { return com.luka.carplay.bus.CarplayBus.getInstance(); }
    public void start() { bus().start(); }
    public void stop() { bus().stop(); }
    public void flush(long timeoutMs) { /* Legacy bus had no outbound commands. */ }
    public boolean isConnected() { return bus().isConnected(); }
    public void on(int type, final Listener l) {
        bus().on(type,l==null ? null : new com.luka.carplay.bus.CarplayBus.Listener() {
            public void onFrame(int t,int f,byte[] p,int n) { l.onFrame(t,f,p,n); }
        });
    }
    public void off(int type) { bus().off(type); }
    /* Preserve legacy input ABI: its unsupported cursor commands remain disabled.
       Allemon RGI uses the new generation-fenced bus directly. */
    public void send(int type,int flags,byte[] payload,int len) { }
    public void sendText(int type,int flags,String text) { }
    public void sendBare(int type,int flags) { }
}
