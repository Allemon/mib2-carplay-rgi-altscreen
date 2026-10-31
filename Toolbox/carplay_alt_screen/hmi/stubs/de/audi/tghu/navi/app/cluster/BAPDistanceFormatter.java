package de.audi.tghu.navi.app.cluster;

import de.audi.atip.log.LogChannel;

/* Compile-time descriptor stub only; never packaged. */
public class BAPDistanceFormatter {
    public static class BAPDistance {
        public int getValue() { return 0; }
        public int getUnit() { return 0; }
    }
    public BAPDistanceFormatter(LogChannel log) {}
    public BAPDistance formatDistanceToTurn(int meters, boolean metric) {
        return new BAPDistance();
    }
    public BAPDistance formatDistanceToDestination(int meters, boolean metric) {
        return new BAPDistance();
    }
}
