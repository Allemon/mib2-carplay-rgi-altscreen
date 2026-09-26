# MHI2Q CarPlay Virtual Cockpit Integration (AltScreen + Route Guidance)

**English** | [Українська](README_UA.md)

Unified CarPlay patch set for Audi MHI2Q infotainment with Audi Virtual Cockpit.  
Integrates **[MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)** (CarPlay instrument cluster video streaming) with **[mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)** (3D turn-by-turn route guidance & maneuver renderer) into one single codebase and all-in-one SD card build.

**Tested & verified on:** Audi Q5 (FY) 2019 · `MHI2Q_ER_AUG22_P5092` · MU Software `1329`.

**Disclaimer:** Use at your own risk. These patches modify firmware binaries and system configurations on your infotainment unit. Always back up all original files before making any changes. The authors are not responsible for any damage, bricked devices, or warranty issues resulting from use of these patches.

## 🖼️ Gallery

**AltScreen: CarPlay instrument cluster video (Google Maps) on the Virtual Cockpit**

<p align="center">
  <img src="assets/gallery/vc_altscreen_classic.jpg" width="45%" />
  <img src="assets/gallery/vc_altscreen_sport.jpg" width="45%" /><br />
  <sub>CarPlay cluster video stream with full navigation map & maneuver overlay on Audi Virtual Cockpit (tested on Audi Q5 FY, MU1329)</sub>
</p>

**Steering-wheel zoom on Virtual Cockpit**

<p align="center">
  <img src="assets/gallery/zoom_demo.gif" width="45%" /><br />
  <sub>Zooming the CarPlay cluster map directly via the left steering-wheel roller</sub>
</p>

**Virtual Cockpit: route guidance from the 3D maneuver renderer**

<p align="center">
  <img src="assets/gallery/maneuver_demo.gif" width="90%" /><br />
  <sub>Cluster maneuver renderer driven through a demo route</sub>
</p>

<p align="center">
  <img src="assets/gallery/vc_day_nav.jpeg" height="200" />
  <img src="assets/gallery/vc_night_nav.jpeg" height="200" />
</p>
<p align="center">
  <img src="assets/gallery/vc_full_map.jpeg" height="200" />
  <img src="assets/gallery/vc_lane_guidance.jpeg" height="200" />
</p>

**Audi front PDC no longer hides CarPlay** · **Cover art on the cluster**

<p align="center">
  <img src="assets/gallery/pdc_over_carplay.jpeg" width="45%" />
  <img src="assets/gallery/cover_art.jpeg" width="45%" />
</p>

**Head-up display**

<p align="center">
  <img src="assets/gallery/IMG_0623.jpeg" width="30%" />
  <img src="assets/gallery/IMG_6302.jpeg" width="30%" />
  <img src="assets/gallery/IMG_0599.jpeg" width="30%" />
</p>

## 📍 Contents

- [Gallery](#-gallery)
- [Features](#-features)
- [Repository layout](#-repository-layout)
- [Build](#-build)
- [Deployment](#-deployment)
- [Logging](#-logging)
- [Documentation](#-documentation)
- [Help wanted](#-help-wanted)
- [References](#-references)

## ✨ Features

There is nothing to switch on: plug in the iPhone and CarPlay starts as usual; the cluster
features below follow it automatically.

- **AltScreen: CarPlay cluster video on the Virtual Cockpit.** Renders the second-screen CarPlay video
  stream (Apple Maps full cluster map) directly inside the Audi Virtual Cockpit via displayable 3.
- **Smooth 30 fps cluster video.** Upstream AltScreen showed ~15 fps: its frame tap read back only
  every second decoded frame, and the mirror sidecar polled every 20 ms on a fixed 33 ms period.
  The SD build patches both binaries, so the cluster shows 28-30 of the iPhone's 30 fps
  (car-verified, `MIRROR_PRESENT_FPS` in STATUS).
- **Corrected aspect ratio (no distortion).** The mirror sidecar was rebuilt with corrected 1:1 aspect ratio
  geometry (clean bottom crop) so the CarPlay cluster video is not stretched or squashed on the Virtual Cockpit.
- **Clean OEM look (watermark removed & Audi logo).** Upstream promotional watermarks are removed
  (transparent overlay), and the startup screen uses a clean Audi logo (`logo.rgba`) instead of third-party branding.
- **Turn-by-turn route guidance + 3D maneuver renderer.** Real-time 3D maneuver arrow, lane guidance,
  distance to turn, remaining distance and arrival time. When AltScreen video is active, `ScreenModule`
  automatically selects **Display Context 81** (`{98, 101, 102, 3}`), compositing the custom 3D
  maneuver overlay and KDK backings cleanly on top of the live video stream.
  Needs an app that sends CarPlay route guidance: Apple Maps and Google Maps do, AMap does with its
  CarPlay guidance setting on, Waze does not
  ([details](docs/rgd/rgd-activation.md#-which-navigation-apps-send-route-guidance)).
- **Seamless automatic fallback (Context 80 / 74).** When the AltScreen video stream is idle, not ready,
  or running in standalone route-guidance mode, `ScreenModule` seamlessly falls back to **Context 80**
  (3D maneuver arrow over the stock native Audi map) or resting **Context 74**
  ([details](docs/cluster/display-contexts.md)).
- **Route text in the Virtual Cockpit.** A text line names the exit sign or the next road (the
  current road when there is nothing else); long names scroll. Press **OK** (the left steering-wheel
  roller) to switch it to arrival time and time left, and press again to go back; it returns by itself
  after 20 s ([details](docs/rgd/vc-route-text.md)).
- **Head-up display.** The same maneuver icons, lane arrows and distance appear on the HUD.
- **Steering-wheel roller zoom for CarPlay map & stock map.** While the AltScreen CarPlay video is on
  the Virtual Cockpit, each click of the left steering-wheel roller sends the factory AirPlay
  `changeMapZoomLevel` command to the iPhone for the cluster display (matching OEM CarPlay behavior).
  Scrolling away zooms out, scrolling towards you zooms in. The native Audi map beneath the video also
  continues to zoom ([details](docs/input/steering-wheel.md)).
- **Cluster map layout selector (car marker centering).** To address the car location marker
  offset caused by iOS reserving space for its own maneuver card, the MMI-Cockpit-Carplay GEM menu
  provides a **Cluster map layout** selector with four options: *AltScreen default*, *maneuver card on top*,
  *maneuver card on the right*, and *no ETA* (applied on next phone reconnect).
- **Cover art on the cluster.** The now-playing album art shows on the cluster media screen.
- **Parking popups no longer hide CarPlay.** When the Audi front PDC / parking view pops up beside it,
  CarPlay stays on screen instead of being replaced ([details](docs/hmi/pdc-small-stage.md)).
- **MMI touchpad → DPAD bridging** so finger drags navigate CarPlay menus.

## 🗂️ Repository layout

| Path | Purpose |
| --- | --- |
| `hook/` | Shipping native `libcarplay_hook.so` source |
| `java_patch/` | The only supported Java patch source |
| `java_resources/` | Resources packed into the jar (VC glyph-width / Unicode table `vc-text.bin`) |
| `maneuver_render/` | GLES maneuver overlay renderer (C, plus the C++11 `scene/` engine) |
| `common/` | Shared renderer code: QNX Screen surface, GL program-binary cache, log timestamps |
| `deploy/smartphone_integrator/` | Runtime scripts and child-process configuration for the HU |
| `install_MoreIncredibleBash/`, `uninstall_MoreIncredibleBash/`, `logging_MoreIncredibleBash/` | M.I.B. custom scripts that install / remove a staged release / collect logs |
| `altscreen/` | AltScreen SD tree (CarPlay video on the VC) with RGI wired into its installer; `scripts/build_sd.sh` turns it into a card |
| `deploy/altscreen/` | The AltScreen variant of the SI carplay child (`CARPLAY_PRELOAD_EXTRA`) |
| `tools/` | AltScreen 30 fps binary patches (`patch_altscreen_fps.py`) and the cluster H.264 dump analyser |
| `scripts/` | Docker build entry points (Java / hook / renderer / SD card) and host test runners |
| `tests/` | Host tests (C, Java, Python) for the hook, Java bridge and renderer |
| `toolchain/qnx65-abi/` | QNX Screen ABI headers used only for cross-compilation |
| `docs/` | Markdown knowledge base (also opens in Obsidian) - validated RE + implementation notes (open [`docs/INDEX.md`](docs/INDEX.md)) |
| `assets/` | Screenshots and visual reference material |
| `build/` | Canonical deployable artifacts |

Raw unit logs and generated class trees are intentionally kept outside Git.

## 🔧 Build

Native code needs the QNX 6.5 ARMv7 cross-toolchain image from
[luka-dev/qnx65-armv7-toolchain](https://github.com/luka-dev/qnx65-armv7-toolchain). Build it once:

```sh
git clone https://github.com/luka-dev/qnx65-armv7-toolchain
cd qnx65-armv7-toolchain
./host-scripts/qnx-run.sh build        # qnx65-armv7-toolchain:latest (GCC 8.5)
```

Then run from this repository's root:

```sh
./scripts/build_java.sh        # → build/carplay_hook.jar
./scripts/build_hook.sh        # → build/libcarplay_hook.so
./scripts/build_renderers.sh   # → build/maneuver_render
```

All three build in Docker - no host toolchain required. The Java patch compiles in a pinned
`eclipse-temurin:8` container (against the stock jar + OSGi libs under `../../Tools/jxe2jar`; the
scripts expect the author's `out/MU1316-final.jar`, so if your own stock jar is named or located
differently, adjust the path in `scripts/build_java.sh` and the test scripts); the two
native builds use the `qnx65-armv7-toolchain` image and synthesize their import stubs, so the resulting
ELF binds the unit's real Screen/EGL/GLES libraries at runtime. The renderer's C++ scene engine is
built with that image's `g++` and must not pull in the C++ runtime; the hook build rejects any dynamic
export beyond its five interposers. There are no Java variants.

There is one hook image: logging is always compiled in, WARN/ERROR by default, INFO with the
`carplay_verbose` marker (see [Logging](#-logging)). The only build-time switch is for debugging:

```sh
./scripts/build_hook.sh                        # production image
LOG_RGD_PACKET_RAW=1 ./scripts/build_hook.sh   # + raw RGD packet hex dumps
```

### AltScreen SD card (CarPlay video on the VC + route guidance)

`altscreen/` holds the [MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)
SD tree (CarPlay's instrument-cluster video on the Virtual Cockpit) with this project's route
guidance wired into its installer. One command builds everything and stages a ready card:

```sh
STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh                  # -> build/sd/
SD=/Volumes/SD32 STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh   # ... and copy onto the card
```

It fills in the JAR size/cksum that AltScreen's INSTALL/START/STATUS pin and regenerates
`SHA256SUMS-SD.txt`; the card's `MMI-Cockpit-Carplay/` (AltScreen stock backups) is never touched.
On the car: **Toolbox -> Update Toolbox**, then **MMI-Cockpit-Carplay -> INSTALL**, reboot, **START**,
reboot. While the video runs the cluster uses ctx 81 (`{98, 101, 102, 3}`, maneuver panel over the
video); without it route guidance falls back to ctx 80 over the stock map
([display-contexts](docs/cluster/display-contexts.md)). **STATUS** reports `DIO_PRELOAD_ALTSCREEN`,
`DIO_PRELOAD_RGI`, `RGI_*` and the live context. **RESTORE ORIGINAL** removes both.
The steering-wheel roller zooms the CarPlay cluster map as it zooms the native one
([steering-wheel](docs/input/steering-wheel.md)); the GEM menu's **Cluster map layout** entries pick
the iPhone's cluster presentation (applied on the next phone connect).
The build also patches AltScreen's `libcarplay_altscreen.so` and mirror sidecar for 30 fps
cluster video (`tools/patch_altscreen_fps.py`; each patch checks the exact upstream binary first);
`ALTSCREEN_FULL_FPS=0` ships them unchanged. GEM **Dump cluster video to SD (diagnostic)** saves
the last ~4 MB of the cluster H.264 stream to `MMI-Cockpit-Carplay/logs/h264/`;
`python3 tools/h264_ring_analyze.py <file>` shows what the iPhone sent.
End-to-end check of the package (needs an AltScreen stock backup from a unit):
`FIXTURE=<card>/MMI-Cockpit-Carplay/backup ./scripts/test_altscreen_e2e.sh`.

#### Key enhancements over upstream AltScreen:
- **30 fps instead of 15:** the frame tap reads back every decoded frame and the mirror sidecar polls every 4 ms with a one-vsync minimum period (binary patches applied by `build_sd.sh`).
- **Steering-wheel cluster map zoom:** Each roller click sends `changeMapZoomLevel` directly to iOS via AirPlay (`CRSUIClusterZoomAction`); scrolling away zooms out, scrolling towards zooms in.
- **Cluster map layout selector (vehicle marker centering):** GEM menu provides four selectable layouts (`AltScreen default`, `maneuver card on top`, `maneuver card on the right`, `no ETA`) to balance map layout and vehicle marker centering without obscuring navigation.
- **Corrected aspect ratio & projection:** Instead of stretching or squashing the image, the mirror sidecar is rebuilt with a 1:1 aspect ratio and clean bottom crop so that maps and road geometry display with natural proportions.
- **Removed watermarks:** Upstream advertising and promotional text watermarks are removed (`watermark.rgba` is completely transparent).
- **OEM Audi startup logo:** Replaced third-party repository startup branding with an authentic Audi logo (`logo.rgba`).
- **Seamless RGI companion:** The integrated `rgi_companion.sh` automatically wires both the AltScreen universal preload and `libcarplay_hook.so` via `CARPLAY_PRELOAD_EXTRA` without clobbering the environment.

### Tests

Host-only, no unit needed:

```sh
./scripts/run_tests.sh            # C + shell: RGD parser, bus, cover art, shader cache, installer, supervisor
./scripts/test_route_info.sh      # Java route-guidance / BAP bridge against the stock interfaces
./scripts/test_java_transports.sh # Java bus + renderer sockets, touchpad
./scripts/test_maneuver_native.sh # renderer engine + lanes (macOS, ASan/UBSan)
```

The Java suites need the stock MU1316 jar and JDK under `../../Tools/jxe2jar`. Full toolchain,
threading, boot and the complete test list live in the knowledge base - see
[`docs/architecture.md`](docs/architecture.md).

## 🚀 Deployment

**Compatibility & Tested Hardware:**
- **Confirmed & verified working on:**
  - **Vehicle:** Audi Q5 (FY) 2019
  - **Firmware Release:** `MHI2Q_ER_AUG22_P5092`
  - **MU Software:** `1329`
- **Supported units:** Any Audi MHI2Q infotainment unit (developed and tested on MU1316 and MU1329).
  What matters is:
  - A fully digital instrument cluster (**Audi Virtual Cockpit**); analog clusters are not supported.
  - Preferably the latest firmware available for the unit, flashed before installing.

---

### Option 1: Unified AltScreen + Route Guidance SD Card (Recommended)

This installs both **AltScreen** (full CarPlay cluster video stream on the Virtual Cockpit) and **Route Guidance Integration (RGI)** (3D maneuver arrow overlay, lane guidance, route text, HUD) using the integrated MIB2 Toolbox.

1. **Build the SD card image:**
   ```sh
   STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh                  # stages files into build/sd/
   SD=/Volumes/SD32 STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh   # or copies directly onto your SD card
   ```
2. **Insert the SD card** into slot 1 (SD1) of the MMI unit.
3. **Open GEM** (Green Engineering Menu):
   - Go to **Toolbox -> Update Toolbox** to load the scripts.
   - Go to **MMI-Cockpit-Carplay -> INSTALL**. The companion installer (`rgi_companion.sh`) automatically installs the native RGI files to `/mnt/app/root/hooks/`, places the unified HMI jar `carplay_hook-basevideo3.jar`, and patches `smartphone_integrator.json` (with `CARPLAY_PRELOAD_EXTRA`) and `dio_manager.json`.
4. **Reboot the unit** normally.
5. **Open GEM -> MMI-Cockpit-Carplay -> START** to activate the mirror display service.
6. **Reboot the unit.**

**On-Car Verification & Features:**
- **Steering-wheel zoom:** Start CarPlay with the navigation map on the Virtual Cockpit and turn the left steering-wheel roller ("away" = zoom out, "towards" = zoom in).
- **Vehicle marker alignment:** Build a route in CarPlay, navigate to **MMI-Cockpit-Carplay -> Cluster map layout** in GEM, and test the four options (`AltScreen default`, `maneuver card on top`, `maneuver card on the right`, `no ETA`), reconnecting the phone after each to see which layout gives the most centered car marker on your cluster view (Classic / Sport).
- **STATUS:** Run **MMI-Cockpit-Carplay -> STATUS** in GEM to inspect system health. It reports `DIO_PRELOAD_ALTSCREEN`, `DIO_PRELOAD_RGI`, `RGI_*` status, and the live display context from `/tmp/carplay_cluster.ctx`.
- **RESTORE ORIGINAL:** Selecting **RESTORE ORIGINAL** in GEM cleanly removes all patches and restores the stock firmware configuration from backup.

---

### Option 2: Standalone Route Guidance (Without AltScreen Video)

For users who want route guidance and 3D maneuver arrows drawn only over the native Audi cluster map without the CarPlay video stream.

A release consists of eight files plus two config edits; nothing stock is replaced:

| On-unit path | Files |
| --- | --- |
| `/mnt/app/root/hooks/` | `libcarplay_hook.so`, `maneuver_render` (from `build/`), `flag_atlas.rgba` (from `maneuver_render/resources/`), `carplay_startup.sh`, `carplay_monitor.sh`, `carplay_processes.sh`, `carplay_cleanup.sh` (from `deploy/smartphone_integrator/`) |
| `/mnt/app/eso/hmi/lsd/jars/` | `carplay_hook.jar` (from `build/`) |
| `/mnt/system/etc/eso/production/smartphone_integrator.json` | `children.carplay` replaced by [`carplay_child.json`](deploy/smartphone_integrator/carplay_child.json) |
| `/mnt/system/etc/eso/production/dio_manager.json` | `MessagesSentByAccessory` += `0x5200`, `0x5203`; `MessagesReceivedFromDevice` += `0x5201`, `0x5202`, `0x5204` |

Both the `dio_manager.json` IDs and the hook's runtime Identify patch are required: without the IDs
iOS sends route guidance and the SDK silently drops it.

- **With M.I.B. (recommended for standalone RGI):** Copy `install_MoreIncredibleBash/` to the M.I.B. SD card and drop
  all release assets straight into `mod/carplay/` (the eight files above plus `carplay_child.json`), then run
  **GEM -> M.I.B. -> Advanced Settings -> Run Custom Script** with CarPlay disconnected. To remove, run
  `uninstall_MoreIncredibleBash/`.
- **Manually:** Over root shell (SSH or Telnet). See [`docs/deploy/install.md`](docs/deploy/install.md) for the complete
  step-by-step guide.

**Reboot.** Disconnect CarPlay, run `sync` and wait a few seconds, then reboot normally: a forced
reboot (the MMI button combo) right after copying can leave files truncated or missing. On boot
`smartphone_integrator` launches everything; check the logs in `/tmp`.

Exact ownership rules, the `LD_PRELOAD`/env constraints and the MU1316 QNX-compat audit are in
[`deploy/smartphone_integrator/README.md`](deploy/smartphone_integrator/README.md).

## 📝 Logging

Everything logs to `/tmp` on the unit:

| File | Source |
| --- | --- |
| `/tmp/carplay_hook.log` | native hook (inside `dio_manager`) |
| `/tmp/carplay_java.log` | Java patch (bounded + rotated, `.1` = previous) |
| `/tmp/maneuver_render.log` | cluster maneuver renderer |
| `/tmp/carplay_wrapper.log` | startup wrapper, preload merger (`CARPLAY_PRELOAD_EXTRA`), and renderer monitor |
| `/tmp/carplay_cluster.ctx` | active cluster display context (`81` = AltScreen video + maneuver, `80` = maneuver on stock map, `74` = idle) |

By default only warnings and errors are recorded. To capture **everything** (lift hook and Java to
`INFO`), drop a marker file on the unit - no rebuild needed:

```sh
touch /mnt/app/carplay_verbose        # survives reboot; /tmp/carplay_verbose does not
```

Hook and Java read the marker at every CarPlay session start, so it takes effect on the next phone
connect - no reboot. Remove the marker to return to the quiet default. Logs reset on reboot, so pull
them before restarting.

For raw route-guidance packet dumps, rebuild the hook with `LOG_RGD_PACKET_RAW=1` (see [Build](#-build)).

**No shell? Use M.I.B.** Copy `logging_MoreIncredibleBash/` to the card and run it like the installer.
Each run saves everything to `<card>/carplay_logs/NNN/` and then creates `/tmp/carplay_verbose`: run it
once, reconnect the phone and drive with CarPlay, run it again - the second folder holds the verbose
session. Attach that folder to a bug report.

## 📚 Documentation

`docs/` is a Markdown knowledge base (also opens in Obsidian) - one note per topic, each fact validated against code /
firmware / iOS binary. Start at [`docs/INDEX.md`](docs/INDEX.md): architecture & threading, the hook
and bus, route guidance (TLV → BAP → cluster, lanes, route text), cluster compositing and the maneuver
renderer, input, deploy/connect, build & host tests, the reverse-engineering references, and a
per-note verification status.

## 🤝 Help wanted

PRs are welcome - bug fixes, new maneuver cases, docs, on-car test reports.

**Reporting a bad maneuver icon.** The iAP2→BAP mapping covers all 54 CarPlay maneuver types but has
only been exercised on a limited set of real routes. A snippet of `/tmp/carplay_hook.log` from the
moment plus a note on what was expected helps a lot. The hook logs unrecognised route-guidance messages
as `[HOOK] Unknown 0x52xx msgid=0xNNNN dir=IN len=N` followed by a hex dump - that line is the best
starting point when iOS sends a maneuver type we don't handle yet.

## 🔗 References

Thanks for the prior work and knowledge that helped figure this out.

- [yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen) — Virtual Cockpit CarPlay video mirror sidecar, universal preload, and MIB2 Toolbox installer (by yuedizhibo and Lanye-z).
- [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) — Turn-by-turn route guidance, 3D maneuver renderer, and BAP integration.
- https://github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering
- https://github.com/EthanArbuckle/iPhone18-3_26.1_23B85_Restore
- https://github.com/adi961/mib2-android-auto-vc
- [@fifthBro](https://t.me/fifthBro)
