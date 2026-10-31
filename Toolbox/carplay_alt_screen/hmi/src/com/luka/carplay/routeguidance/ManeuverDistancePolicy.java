package com.luka.carplay.routeguidance;

/** Lanye distance policy shared by BAP/HUD and the GPU arrow renderer. */
public final class ManeuverDistancePolicy {
    public static final int CITY_METERS=350;
    public static final int LONG_STEP_METERS=1000;
    public static final int LONG_STEP_BOUNDARY_METERS=2000;
    private ManeuverDistancePolicy() { }
    public static int threshold(int rawStepMeters, boolean highwayType) {
        boolean longStep=rawStepMeters>0 ? rawStepMeters>LONG_STEP_BOUNDARY_METERS : highwayType;
        return longStep ? LONG_STEP_METERS : CITY_METERS;
    }
    public static boolean showReal(int distance, int threshold, boolean arrival,
                                   boolean startup, boolean previous) {
        if(arrival) return true;
        if(distance>0) return distance<=threshold;
        return startup || previous;
    }
    public static RendererMapper.Mapping map(boolean real, int[] bap, int side, int type,
                                              int angle, boolean present, int[] roads) {
        if(real) return RendererMapper.map(bap[0],bap[1],side,type,angle,present,roads);
        RendererMapper.Mapping straight=new RendererMapper.Mapping();
        straight.icon=RendererMapper.ICON_APPROACH;
        straight.junctionAngles=new int[0];
        return straight;
    }
}
