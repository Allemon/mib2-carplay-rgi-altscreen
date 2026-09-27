---
title: Java production cleanup audit - reconciled to current branch
tags: [maintenance, java, verified]
status: verified-source
sources:
  - code: java_patch/com/luka/carplay/rgd/BAPBridge.java
  - code: java_patch/com/luka/carplay/rgd/RouteGuidance.java
  - code: java_patch/com/luka/carplay/core/ScreenModule.java
  - code: java_patch/com/luka/carplay/core/SteeringWheelInputModule.java
  - code: java_patch/com/luka/carplay/rgd/RendererServer.java
  - code: java_patch/com/luka/carplay/core/FrameworkRef.java
reconciles:
  - docs/reference/JAVA_PRODUCTION_CLEANUP_AUDIT.md
---

# Java production cleanup audit - reconciled to current branch

Migrated from `docs/reference/JAVA_PRODUCTION_CLEANUP_AUDIT.md` (original scope: `java_patch/`
against stock MU1316) and **re-verified against the current branch**. Each item below carries its
**real current status**, not the audit's original status - several cleanups have since landed.

## 📋 Context

> [architecture](../architecture.md) - Java patch layer -> cluster HUD + route-info - KOMO gfx gate -> [komo-widget-video](../re/firmware/komo-widget-video.md)
> - route-info toggle publishes [bap-fctids](../rgd/bap-fctids.md) FctID 22

**Bottom line:** all four cleanup groups are resolved on this branch except two dead-code leftovers:
the `FrameworkRef` accessors, and `ClusterService.refreshInitializingScreenAfterCarPlay()`, which came
back with the ported `ClusterService` but has no caller.

## ✅ [x] route-info phase - live, keep (was: closed)

(Line numbers in this section predate the RGI port; the call chain itself is unchanged.)

The historical "phase can never leave 0, remove it" finding is **resolved and the path is live** -
do not remove it. It is now driven by the steering-wheel roller press:

`SteeringWheelInputModule` (`SteeringWheelInputModule.java:223`) ->
`ScreenModule.onSteeringWheelOkPressed()` (`ScreenModule.java:225-229`) ->
`RouteGuidance`'s `InfoModeListener.onInfoModeToggle()` (`RouteGuidance.java:68-74`) ->
`requestInfoModeToggle()` (`RouteGuidance.java:577-587`, `desiredInfoPhase ^= 1`) ->
`BAPBridge.infoPhase` and the trip-summary FctID 22 path.

`ScreenModule.InfoModeListener` is a real registered interface (`ScreenModule.java:210-229`), bound
by `RouteGuidance` in start (`RouteGuidance.java:299,321`) and cleared on stop
(`RouteGuidance.java:384`). `BAPBridge.infoPhase` / `buildTripSummary` / `lastDistanceToDestinationM`
are therefore reachable and must stay. See [steering-wheel](../input/steering-wheel.md).

## ✅ [x] fake ClusterService pipeline API - removed (was: to remove)

Confirmed gone: `activateCustomRendererPipeline()` and `deactivateCustomRendererPipeline()` no longer
exist in `java_patch/`. (!) `refreshInitializingScreenAfterCarPlay()` is back in `ClusterService`
(ported from mhi2, where it restores the stock INITIALIZING screen after an altScreen takeover) but
nothing calls it here - remove it or wire it deliberately. The
`BAPBridge` branch that read the constant "readiness" string is gone too. Real readiness
(`RendererServer.isReady()/isFrameReady()` + frame-event gate) remains authoritative. The four live
`ClusterService` accessors (`getDSIResponseContainer()`, `triggerRefreshRGIValid()`, and the CombiBAP
getter/setter) are retained as intended.

## ✅ [x] KOMO reflection ladder (`forceGfxAvailable`) - resolved

The reflection ladder is gone (`CoverArtProviderMux` is now the only `java.lang.reflect` user).
`BAPBridge.forceGfxAvailable` returns immediately unless `Util.isClusterMapMOST(fw)`; on a MOST cluster
it calls the public `KOMOService.updateDataRate(rate, 1)` then `updateGfxState(gfx, 1)` - data rate
**before** gfx availability - falling back to `ClusterViewMode.setDataRate` / `setGFXAvailable` +
`ClusterService.setKOMODataRate` when no `KOMOService` was acquired. On this FPK cluster it therefore
does nothing: every data-rate write used to run `ClusterViewMode.setDataRate -> refreshMapVisibility`,
which parked the stock kombi map in its hidden context after a route (frozen map, roller zoom
swallowed) until the VC re-sent MapViewAndOrientation. See [komo-widget-video](../re/firmware/komo-widget-video.md).

## ⚠️ (!) disabled diagnostics & dead members - PARTIALLY DONE

**Done (verified gone):**

- `traceBap()` / `traceDescriptor()` / `BAP_TRACE_ENABLED` and every trace call site - removed.
- All `Log.d()` call sites - **0 remaining** in `java_patch/`. Logger and C hook default to WARN
  (see `docs/reference/PRODUCTION_LOGGING.md`). `Log.i` deliberately kept.
- `CarPlayApp.fw()`, `CarplayBus.isConnected()` / `Data.bool()` / `Data.strList()`,
  `BAPBridge.isActionBlinkThreadRunning()`, `EXITVIEW_ROW/EXITVIEW_ASIA`,
  `AltScreenModule.isClusterActive()`, `ROUTE_STATE_ACCEPT_ALL_MANEUVER_IDX` - all removed.

**Still open (dead code, no callers found):**

- `FrameworkRef.context()` / `deviceManager()` / `hmiServiceApp()` (`FrameworkRef.java:36-40`) -
  no call sites; drop these and their now-unused imports.
- `ClusterService.refreshInitializingScreenAfterCarPlay()` - see above.

(`RendererServer.isConnected()` is live again: `BAPBridge` uses it in renderer recovery.)

(Note: `ScreenModule.isConnected()` is a **different, live** method - it pins the cluster while
CarPlay owns it, called from `CombiMapController` and `ClusterService`. Keep it.)

## 📌 Keep (unchanged - do not delete for size)

- The six stock replacement classes: their public/protected ABI is complete and stock code calls
  into them outside local reachability.
- `CarplayBus` (:19810) and `RendererServer` (:19800) - different peers/protocols.
- `RgdModule`, `FrameworkRef.ServiceHandle`, `Module` - ordered lifecycle + paired OSGi release.
- Mapper classes, lane fallback.
- The cover-art replacement/provider/mux chain (live; intentionally preserves the stock provider).
- Trip-summary ETA helpers (`lastEtaSeconds`, `lastTimeRemainingSeconds`,
  `lastTimeRemainingSampleSeconds`, `currentRemainingSeconds()`, `currentArrivalSeconds()`) - they
  publish the real FctID 22 absolute ETA.

## ✍️ Remaining patch order

1. Remove the dead `FrameworkRef` accessors (+ unused imports) and decide on
   `refreshInitializingScreenAfterCarPlay()`.
2. Rebuild, rerun the stock-ABI/linkage audit (`scripts/audit_java_stock.sh`) and the host suites
   (`scripts/test_route_info.sh`, `scripts/test_java_transports.sh`), then test on the unit: cold boot,
   RGI start/stop, View changes, renderer death/reconnect, stock navigation after disconnect, cover art.
