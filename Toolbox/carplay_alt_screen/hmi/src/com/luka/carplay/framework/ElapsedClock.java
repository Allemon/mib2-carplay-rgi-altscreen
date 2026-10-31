package com.luka.carplay.framework;
public final class ElapsedClock {
    private static long wall = System.currentTimeMillis();
    private static long elapsed = wall;
    private ElapsedClock() { }
    public static synchronized long nowMs() { return advance(System.currentTimeMillis()); }
    static synchronized long advance(long next) {
        if (next >= wall) elapsed += next - wall;
        wall = next;
        return elapsed;
    }
}
