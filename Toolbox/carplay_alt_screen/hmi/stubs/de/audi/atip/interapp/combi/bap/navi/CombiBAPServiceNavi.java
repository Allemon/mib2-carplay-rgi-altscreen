package de.audi.atip.interapp.combi.bap.navi;

import de.audi.atip.interapp.combi.bap.audio.data.CombiBAPTMCInfoMessage;
import de.audi.atip.interapp.combi.bap.navi.data.*;

public interface CombiBAPServiceNavi {
    void updateRGStatus(int a);
    void updateActiveRGType(int a);
    void updateDistanceToNextManeuver(int a, int b, boolean c, int d);
    void updateCurrentPositionInfo(String a);
    void updateManeuverDescriptor(CombiBAPNaviManeuverDescriptor[] a);
    void updateLaneGuidance(boolean a, CombiBAPNaviLaneGuidanceData[] b);
    void updateExitView(int a, int b);
    void updateManeuverState(int a);
    void showInitializingScreen();
    void hideInitializingScreen();
    void updateCompassInfo(int a, int b);
    void updateTurnToInfo(String a, String b);
    void updateDistanceToDestination(int a, int b, boolean c);
    void updateTimeToDestination(int a, int b, long c);
    void updateTMCInfoMessages(CombiBAPTMCInfoMessage[] a);
    void updateLastDestinationsList(CombiBAPDestinationListEntry[] a);
    void updateFavoriteDestinationsList(CombiBAPDestinationListEntry[] a);
    void updateHomeAddress(CombiBAPNaviDestination a);
    void routeGuidanceActDeactResult(int a);
    void repeatLastNavAnnouncementResult(int a);
    void updateVoiceGuidanceState(int a);
    void updateInfoStates(int a);
    void updateTrafficBlockIndication(int a);
    void updateMapColor(int a);
    void updateMapType(int a, int b);
    void updateSupportedMapTypes(boolean a, int b);
    void updateMapView(int a, int b);
    void updateSupportedMapViews(int a, int b);
    void updateMapVisibility(boolean a, boolean b);
    void updateMapOrientation(int a);
    void updateMapScale(int a, boolean b, int c, int d, boolean e);
    void updateDestinationInfo(CombiBAPDestinationInfo a);
    void updateAltitude(int a, int b);
    void updateOnlineNavigationState(int a, int b, int c);
    void updateSemidynamicRouteGuidance(CombiBAPSemiDynamicRouteInfo a);
    void poiSearchResult(int a, int b);
    void updatePOIListSize(int a);
    void updateFSGSetup(int a, boolean b);
    void updateMapPresentation(boolean a, boolean b, boolean c);
    void updateEtcStatus(EtcStatus a);
}
