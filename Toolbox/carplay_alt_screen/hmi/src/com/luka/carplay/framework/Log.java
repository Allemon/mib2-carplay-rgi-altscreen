/*
 * Minimal file-only logger.  Never write to System.out/System.err: lsd routes
 * them through the shared HMI slog path, adding boot-time contention and noisy
 * duplicate records.  /tmp/carplay_hook.log is bounded and rotated so it stays
 * independently SSH-tailable.  Java 1.4 / Foundation 1.1.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package com.luka.carplay.framework;

import java.io.FileOutputStream;
import java.io.File;
import java.io.PrintStream;

public final class Log {
    public static final int LEVEL_DEBUG = 0, LEVEL_INFO = 1, LEVEL_WARN = 2,
        LEVEL_ERROR = 3, LEVEL_NONE = 4;
    public static final int E = LEVEL_ERROR, W = LEVEL_WARN, I = LEVEL_INFO, D = LEVEL_DEBUG;
    private static volatile boolean enabled = true, includeTimestamp = true;
    private static volatile String requestedPath = "/tmp/carplay_hook.log";
    /* Production emits WARN and ERROR only.  INFO carries the state-transition trace that is
     * worth having while diagnosing (session start, module ready, context/geometry decisions);
     * it stays compiled in and is switched on without a rebuild by `touch /mnt/app/carplay_verbose`
     * or /tmp/carplay_verbose.  The markers are read at j9 start and again on every CarPlay
     * session start (refreshLevel), like the hook, so a marker takes effect on the next phone
     * connect without a reboot.  /tmp is cleared by a reboot; /mnt/app survives it. */
    private static final String VERBOSE_MARKERS =
        "/mnt/app/carplay_verbose:/tmp/carplay_verbose";
    private static volatile int level = resolveLevel();

    /** Re-read the markers; called at each CarPlay session start. */
    public static void refreshLevel() { level = resolveLevel(); }

    private static int resolveLevel() {
        try {
            int from = 0;
            while (from < VERBOSE_MARKERS.length()) {
                int sep = VERBOSE_MARKERS.indexOf(':', from);
                String path = sep < 0 ? VERBOSE_MARKERS.substring(from)
                                      : VERBOSE_MARKERS.substring(from, sep);
                if (new File(path).exists()) return I;
                if (sep < 0) break;
                from = sep + 1;
            }
        } catch (Throwable t) {
            /* Never let logging setup break startup. */
        }
        return W;
    }

    private static String FILE = "/tmp/carplay_hook.log";
    private static final long MAX_FILE_BYTES = 512L * 1024L;
    private static PrintStream file;        /* lazily opened; stays null if it can't be created */
    private static boolean fileTried;
    private static long fileBytes;
    private static long nextFileAttemptMs;
    private static final Object LOCK = new Object();
    private static final int QUEUE_CAPACITY = 256;
    private static final String[] queue = new String[QUEUE_CAPACITY];
    private static int queueHead;
    private static int queueTail;
    private static int queueCount;
    private static Thread writerThread;

    private Log() {}

    public static void setLevel(int l) { level = l; }
    public static void setPath(String path) { if (path != null && path.length() > 0) requestedPath = path; }
    public static void setEnabled(boolean value) { enabled = value; }
    public static void setIncludeTimestamp(boolean value) { includeTimestamp = value; }
    public static void write(int value, String tag, String msg) { out(value, levelStr(value), tag, msg); }
    private static String levelStr(int value) {
        return value == E ? "E" : value == W ? "W" : value == I ? "I" : "D";
    }
    public static void hexdump(String tag, String label, byte[] data, int offset, int length, int maxBytes) {
        if (data == null || offset < 0 || offset > data.length || length < 0 || maxBytes < 0) return;
        int count = Math.min(Math.min(length, data.length-offset), Math.min(maxBytes, 256));
        StringBuffer text = new StringBuffer();
        text.append(label).append(' ');
        String digits = "0123456789abcdef";
        for (int i=0; i<count; i++) {
            int b = data[offset+i] & 255;
            text.append(digits.charAt(b >> 4)).append(digits.charAt(b & 15)).append(' ');
        }
        d(tag, text.toString());
    }

    public static void e(String tag, String msg) { out(E, "E", tag, msg); }
    public static void w(String tag, String msg) { out(W, "W", tag, msg); }
    public static void i(String tag, String msg) { out(I, "I", tag, msg); }
    public static void d(String tag, String msg) { out(D, "D", tag, msg); }

    /* throwable overloads (ports pass an exception) */
    public static void e(String tag, String msg, Throwable t) { out(E, "E", tag, msg + " :: " + t); }
    public static void w(String tag, String msg, Throwable t) { out(W, "W", tag, msg + " :: " + t); }

    private static void out(int l, String p, String tag, String msg) {
        if (!enabled || l < level) return;
        if (msg != null && msg.length() > 2048) msg = msg.substring(0, 2048) + " [truncated]";
        String line = (includeTimestamp ? stamp() : "") + " [CP/" + p + "][" + tag + "] " + msg;
        synchronized (LOCK) {
            ensureWriterLocked();
            if (writerThread == null) return;
            if (queueCount == QUEUE_CAPACITY) {
                queue[queueHead] = null;
                queueHead = (queueHead + 1) % QUEUE_CAPACITY;
                queueCount--;
            }
            queue[queueTail] = line;
            queueTail = (queueTail + 1) % QUEUE_CAPACITY;
            queueCount++;
            LOCK.notifyAll();
        }
    }

    /* Same "HH:MM:SS.mmm" wall clock as the hook and renderer logs, so one
     * startup timeline can be read across all of them. */
    private static String stamp() {
        java.util.Calendar c = java.util.Calendar.getInstance();
        int h = c.get(java.util.Calendar.HOUR_OF_DAY), m = c.get(java.util.Calendar.MINUTE);
        int sec = c.get(java.util.Calendar.SECOND), ms = c.get(java.util.Calendar.MILLISECOND);
        StringBuffer b = new StringBuffer(12);
        if (h < 10) b.append('0');
        b.append(h).append(':');
        if (m < 10) b.append('0');
        b.append(m).append(':');
        if (sec < 10) b.append('0');
        b.append(sec).append('.');
        if (ms < 100) b.append('0');
        if (ms < 10) b.append('0');
        b.append(ms);
        return b.toString();
    }

    /* No stock HMI/BAP callback ever performs file I/O. A single daemon owns
     * open/write/flush/rotation; overload drops the oldest diagnostics rather
     * than blocking lsd. */
    private static void ensureWriterLocked() {
        if (writerThread != null) return;
        Thread t = new Thread(new Runnable() {
            public void run() { writerLoop(); }
        }, "carplay-log-writer");
        t.setDaemon(true);
        try {
            t.start();
            writerThread = t;
        } catch (Throwable ignored) {
            writerThread = null;
        }
    }

    private static void writerLoop() {
        while (true) {
            String line;
            synchronized (LOCK) {
                while (queueCount == 0) {
                    try { LOCK.wait(); } catch (InterruptedException ignored) { }
                }
                line = queue[queueHead];
                queue[queueHead] = null;
                queueHead = (queueHead + 1) % QUEUE_CAPACITY;
                queueCount--;
            }
            /* Logging must remain best-effort even if the old Foundation/QNX
             * file stack throws something other than IOException.  Never let
             * the sole writer die while writerThread remains non-null: that
             * would make every later diagnostic queue forever and churn the
             * bounded ring for no useful output. */
            try { writeLine(line); }
            catch (Throwable ignored) { resetFileAfterFailure(); }
        }
    }

    private static void writeLine(String line) {
        if (line == null) return;
        if (!FILE.equals(requestedPath)) {
            resetFileAfterFailure();
            FILE = requestedPath;
            nextFileAttemptMs = 0L;
        }
        PrintStream f = logFile();
        if (f != null) {
            int bytes = line.getBytes().length + 1;
            if (fileBytes + bytes > MAX_FILE_BYTES) {
                rotateFile();
                f = logFile();
            }
            if (f != null) {
                f.println(line);                        /* /tmp/carplay_hook.log (autoflush) */
                fileBytes += bytes;
                if (f.checkError()) resetFileAfterFailure();
            }
        }
    }

    /* Open the file once, best-effort; append + autoflush so a crash still leaves the tail. */
    private static PrintStream logFile() {
        if (!fileTried) {
            if (ElapsedClock.nowMs() < nextFileAttemptMs) return null;
            fileTried = true;
            try {
                File current = new File(FILE);
                if (current.length() >= MAX_FILE_BYTES) rotateFiles(current);
                if (current.length() >= MAX_FILE_BYTES) {
                    resetFileAfterFailure();
                    return null;
                }
                fileBytes = current.exists() ? current.length() : 0L;
                file = new PrintStream(new FileOutputStream(FILE, true), true);
            }
            catch (Throwable t) { resetFileAfterFailure(); } /* logging is best-effort */
        }
        return file;
    }

    private static void rotateFile() {
        if (file != null) {
            try { file.close(); } catch (Throwable t) { }
            file = null;
        }
        try { rotateFiles(new File(FILE)); }
        catch (Throwable t) { }
        fileTried = false;
        fileBytes = 0L;
        nextFileAttemptMs = 0L;
    }

    private static void resetFileAfterFailure() {
        if (file != null) {
            try { file.close(); } catch (Throwable ignored) { }
            file = null;
        }
        fileTried = false;
        fileBytes = 0L;
        nextFileAttemptMs = ElapsedClock.nowMs() + 5000L;
    }

    private static void rotateFiles(File current) {
        File old = new File(FILE + ".1");
        if (old.exists()) old.delete();
        if (current.exists() && (current.length() > MAX_FILE_BYTES || !current.renameTo(old))) {
            // QNX /tmp is shared memory: atomic rename is unsupported.
            // Truncate the same path rather than reopening it in append forever.
            try { new FileOutputStream(FILE, false).close(); }
            catch (Throwable t) { }
        }
    }
}
