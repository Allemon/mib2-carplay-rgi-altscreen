package de.audi.tghu.navi.app;

import de.audi.tghu.navi.app.cluster.ClusterService;

/* Compile/test descriptor stub only. Vehicle class is supplied by the HMI. */
public class Navigation {
    private static Navigation instance;
    private ClusterService clusterService;

    public Navigation() {}
    public Navigation(ClusterService service) { clusterService = service; }

    public static Navigation getInstance() { return instance; }
    public static void setInstance(Navigation value) { instance = value; }

    public ClusterService getClusterService() { return clusterService; }
    public void setClusterService(ClusterService value) { clusterService = value; }
}
