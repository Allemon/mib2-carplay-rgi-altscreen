---
title: Cluster display contexts & the switch worker
tags: [cluster, displaymanager, verified]
status: verified-source
sources:
  - code: java_patch/de/audi/tghu/fwhmi/DisplayManagerMIB2High.java
  - code: java_patch/com/luka/carplay/core/ScreenModule.java
  - code: java_patch/com/luka/carplay/cluster/ClusterLayerController.java
  - code: maneuver_render/platform_qnx.c
  - code: common/cluster_surface.c
reconciles:
  - docs/reference/dmdt_mu1316_analysis.md
  - docs/reference/gfx_available_root_cause.md
---

# Cluster display contexts & the switch worker

How the instrument cluster (LVDS2 / terminal 1) is composed, and how the patch owns exactly one
custom context without fighting native nav.

## 📋 Context

> [rgd-activation](../rgd/rgd-activation.md) - setNavActive -> **display-contexts** - switch ctx 74<->80 -> [compositing](compositing.md) -
> HU encodes -> MOST -> VC. Geometry of the planes: [kdk-geometry](kdk-geometry.md).

## 📊 Displayables

| id | owner | role |
|---:|---|---|
| 33 | stock | native cluster map (also in stock ctx 74) |
| 98 | `maneuver_render` | maneuver overlay, **transparent when idle** ([maneuver-renderer](maneuver-renderer.md)) |
| 101 / 102 | stock (987 Image backings) | KDK backing planes (sport / popup) |

`maneuver_render` opens a managed screen window with `ID_STRING="98"` via `cluster_surface`
(raw `screen_create/manage_window`, no `libdisplayinit`). Id 98 has **no stock owner**, so there is
no last-writer-wins war with native nav - see [compositing](compositing.md).

## 🧭 Contexts (A5-class; declared in `DisplayManagerMIB2High.defineContexts`)

```mermaid
flowchart LR
    accTitle: Stock and CarPlay display contexts
    accDescr: Context 74 holds the stock KDK and native map, context 80 holds the maneuver displayable, KDK backing and map, with RGI BAP start and route end switching between them.

    subgraph c74["dc[74] - stock (idle)"]
        direction TB
        n1["KDK large"] --- n2["KDK small"] --- n3["native map 33"]
    end
    subgraph c80["dc[80] - CarPlay nav"]
        direction TB
        m1["98 maneuver"] --- m2["101/102 KDK backing"] --- m3["native map 33"]
    end
    c74 -->|"RGI BAP start"| c80
    c80 -->|"route ended + VC hid KDK (Fct44)"| c74
```

- **dc[74]** `CTX_MAP_KDK` (stock) - native map + KDK; the cluster's resting state.
- **dc[80]** `CTX_CARPLAY_NAV` = `{98, 101, 102, 33}` - our maneuver over the KDK backings over the
  **stock native map**. z-order = array order (index 0 = front).
- **dc[81]** `CTX_CARPLAY_VIDEO` = `{98, 101, 102, 3}` - the same planes over the CarPlay cluster
  video that the [MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)
  mirror sidecar draws into displayable 3. `ScreenModule` polls its markers
  (`/tmp/mmi-mirror-active` + `/tmp/mmi-mirror-basevideo.ready`, `AltScreenVideo`) while a session is
  connected: video -> 81, else nav -> 80, else 74. Without AltScreen neither marker exists and 81 is
  never selected. The last applied context is written to `/tmp/carplay_cluster.ctx` for status
  scripts.

`getMappedInternalContext` is identity on MIB2High, so `switchContext(80)` lands on exactly the
declared context. G24 clusters have no such composition and the feature is disabled there.

## ⚙️ The switch worker (single serialized writer)

`ScreenModule` runs **one** persistent worker - the sole caller of `switchContext` / `setUpdateRate`,
so two switches can never race:

```mermaid
stateDiagram-v2
    accTitle: Context switch worker states
    accDescr: Entering ctx 80 bounces through context 72 with a settle delay and rate changes, and leaving ctx 80 drops the update rate, switches to 74 and restores the rate.

    direction LR
    [*] --> Stock74
    Stock74 --> Bounce72: enter ctx 80 (encoder was off)
    Bounce72 --> Ctx80: settle 180ms, switchContext(80), setUpdateRate(30)
    Ctx80 --> Stock74: setUpdateRate(0) -> switchContext(74) -> setUpdateRate(30)
```

- `desiredCtx = (connected && navActive) ? 80 : 74` - pure function, worker converges to it
  (reconciled every 250 ms).
- `navActive` is set by `RouteGuidance` on a successful BAP start ([rgd-activation](../rgd/rgd-activation.md)). On route end
  `setNavActive(false)` keeps it true (`navHidePending`) while `ClusterLayerController.isKdkVisible()`,
  i.e. while the VC is still fading its KDK out; `onVcKdkVisibility(false)` (FctID 44) then releases it.
  If the KDK is already hidden, the release is immediate. There is no hold timer.
- Entering 80 from stock needs a real context change first, so the worker **bounces** through ctx 72
  (`CTX_MAP`, kombi-map - never ours) for 180 ms, then switches to 80 and sets 30 FPS. This forces
  `preContextSwitchHook` + the MOST encoder re-point that `switchContext` otherwise short-circuits.
- `isClusterContextWriterThread()` lets `DisplayManagerMIB2High` distinguish our serialized writes
  from stock screen-controller requests while CarPlay owns terminal 1.

On disconnect the worker restores stock 74 (and clears any pending hide); it is never killed, so no
stale per-session worker can outlive its session. Layer opacity inside ctx 80 is
`ClusterLayerController`'s job - see [kdk-geometry](kdk-geometry.md).
