package com.luka.carplay.core;

import com.luka.carplay.CarPlayHook;
import com.luka.carplay.cluster.ClusterStateController;
import com.luka.carplay.cluster.ClusterLayerController;

/** RGI input adapter. ClusterStateController remains the only context writer. */
public final class ScreenModule {
    public static final int VIEWAREA_FULLSCREEN = 0;
    public static final int VIEWAREA_SMALLSCREEN = 1;
    public interface ViewAreaModeListener { void onViewAreaModeChanged(int mode); }
    public interface InfoModeListener { void onInfoModeToggle(); }
    private static volatile ViewAreaModeListener viewListener;
    private static volatile InfoModeListener infoListener;
    private ScreenModule() { }
    public static boolean isConnected() { return ClusterStateController.isCarPlaySessionActive(); }
    public static boolean isNavActive() { return ClusterStateController.isRgiPresentationActive(); }
    public static void setNavActive(boolean active) {
        ClusterStateController.setRgiPresentationActive(active);
    }
    public static void onRendererFrameReady() { ClusterStateController.requestRgiRendererRebind(); }
    public static boolean isSmallScreenViewArea() { return ClusterStateController.isSmallScreenViewArea(); }
    public static void setViewAreaModeListener(ViewAreaModeListener l) { viewListener=l; }
    public static void clearViewAreaModeListener(ViewAreaModeListener l) { if(viewListener==l) viewListener=null; }
    public static void setInfoModeListener(InfoModeListener l) { infoListener=l; }
    public static void clearInfoModeListener(InfoModeListener l) { if(infoListener==l) infoListener=null; }
    public static void onViewAreaModeChanged(int mode) {
        ViewAreaModeListener l=viewListener;
        if(l!=null) l.onViewAreaModeChanged(mode);
    }
    public static void onVcKdkVisibility(boolean visible) { /* OEM geometry owns visibility. */ }
    public static void onSteeringWheelOkPressed() {
        InfoModeListener l=infoListener;
        if(isConnected() && isNavActive() && l!=null) l.onInfoModeToggle();
    }
}
