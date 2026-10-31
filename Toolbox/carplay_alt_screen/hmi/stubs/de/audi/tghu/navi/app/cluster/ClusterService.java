package de.audi.tghu.navi.app.cluster;

import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNavi;

/* Compile/test descriptor stub only; vehicle class is supplied by lsd.jxe/JAR patch. */
public class ClusterService {
    private CombiBAPServiceNavi service;
    private boolean rgiDataValid;
    private final TestDsiContainer dsi = new TestDsiContainer();
    private String testCurrentStreet = null;
    private int testDistanceMeters = -1;
    private long testArrivalMillis = -1L;
    private boolean testArrivalValid;
    private int testFollowInfoFlushCount;
    /* Test-only observation state. The vehicle class is not replaced by this stub. */
    private boolean komoFollowMode;

    public ClusterService() {}
    public ClusterService(CombiBAPServiceNavi value) { service = value; }

    public CombiBAPServiceNavi getCombiBAPListenerCombiService() { return service; }
    public void setCombiBAPListenerCombiService(CombiBAPServiceNavi value) { service = value; }

    public Object getDSIResponseContainer() { return dsi; }
    public void updateRGIString(short[] value) {
        rgiDataValid = value != null && value.length > 0;
    }
    public void updateRgActive(boolean value) {}
    public void updateCurrentStreet(String value) { testCurrentStreet = value; }
    protected void updateDistanceToDestination(int meters, boolean stopover) {
        testDistanceMeters = meters;
    }
    protected void updateArrivalTime(boolean valid, long millis, boolean timezoneOffset) {
        testArrivalValid = valid;
        testArrivalMillis = millis;
    }
    public void updateKOMOFollowInfo() { testFollowInfoFlushCount++; }
    public boolean isRgiDataValidForTest() { return rgiDataValid; }
    public String getTestCurrentStreet() { return testCurrentStreet; }
    public int getTestDistanceMeters() { return testDistanceMeters; }
    public long getTestArrivalMillis() { return testArrivalMillis; }
    public boolean isTestArrivalValid() { return testArrivalValid; }
    public int getTestFollowInfoFlushCount() { return testFollowInfoFlushCount; }
    public void setTestKomoFollowMode(boolean value) { komoFollowMode = value; }
    public TestDsiContainer getTestDsiContainer() { return dsi; }

    public static final class TestDsiContainer {
        private boolean rgActive;
        public boolean isRgActive() { return rgActive; }
        public void setRgActive(boolean value) { rgActive = value; }
    }
}
