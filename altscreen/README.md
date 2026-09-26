# MIB2 Toolbox — CarPlay AltScreen + Route Guidance Integration (RGI)

**English** | [Українська](README_UA.md)

This tree contains the SD-card package for Audi **MHI2Q** infotainment units, enabling the **native CarPlay AltScreen (secondary navigation video stream)** directly on the vehicle's **Virtual Cockpit**, integrated with full 3D turn-by-turn route guidance, steering-wheel map zoom, and vehicle marker centering.

The core display pipeline, watermark removal, aspect-ratio correction, steering-wheel zoom, and RGI integration have been vehicle-validated.

**Tested & confirmed working on:**
- **Vehicle:** Audi Q5 (FY) 2019
- **Firmware Train:** `MHI2Q_ER_AUG22_P5092`
- **MU Software:** `1329`
*(Also compatible with China AUG22 and other MHI2Q units supported by the installer checks).*

> [!IMPORTANT]
> This package modifies head unit configurations and system binaries. Keep the SD card inserted and maintain stable vehicle battery power during installation, start, or recovery operations.  
> **After installation or recovery, fully reboot the head unit (MMI reboot button combo) as instructed before evaluating results.**  
> Do not perform installation, updates, or troubleshooting while driving.

---

## 🖼️ Vehicle Demonstration

<p align="center">
  <img src="../assets/gallery/vc_altscreen_classic.jpg" width="45%" />
  <img src="../assets/gallery/vc_altscreen_sport.jpg" width="45%" /><br />
  <sub>CarPlay cluster video stream with full navigation map & maneuver overlay on Audi Virtual Cockpit (Classic & Sport layouts)</sub>
</p>

<p align="center">
  <img src="../assets/gallery/zoom_demo.gif" width="45%" /><br />
  <sub>Steering-wheel roller zoom on Virtual Cockpit</sub>
</p>

---

## ✨ Features & Enhancements

This package integrates upstream **[MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)** and **[mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)** with local improvements:

- **CarPlay AltScreen on Virtual Cockpit:** Native secondary CarPlay stream (Apple Maps, Google Maps) drawn on displayable 3 (Context 81).
- **Steering-Wheel Roller Zoom:** Scrolling the left steering-wheel roller sends the factory AirPlay `changeMapZoomLevel` command to iOS (`CRSUIClusterZoomAction`), zooming the CarPlay cluster map directly (away = zoom out, towards = zoom in). The native map underneath continues to zoom simultaneously.
- **Cluster Map Layout Selector (Marker Centering):** GEM menu provides four selectable layouts (`AltScreen default`, `maneuver card on top`, `maneuver card on the right`, `no ETA`) to balance map layout and vehicle marker centering.
- **Integrated Route Guidance (RGI):** Turn-by-turn 3D maneuver arrows, distance to turn, remaining time, route text, and HUD integration seamlessly composited over the CarPlay video stream in Display Context 81 (`{98, 101, 102, 3}`).
- **Seamless Fallback:** If the AltScreen video stream is inactive or idle, `ScreenModule` automatically falls back to Context 80 (3D maneuver arrow over native Audi map) or Context 74.
- **Natural 1:1 Aspect Ratio:** The mirror sidecar has been rebuilt with a 1:1 aspect ratio and clean bottom crop so that maps and road geometry display with natural proportions without horizontal or vertical stretching.
- **Clean OEM Aesthetics:** Upstream promotional watermarks are completely removed (`watermark.rgba` is transparent). Startup splash uses an authentic Audi logo (`logo.rgba`) instead of third-party repository branding.
- **Automatic Preload Merging:** `rgi_companion.sh` wires `CARPLAY_PRELOAD_EXTRA` in `smartphone_integrator.json` so the AltScreen universal preload and `libcarplay_hook.so` are cleanly merged for `dio_manager`.
- **Safe State & Backups:** Stock configuration files are safely backed up in `MMI-Cockpit-Carplay/backup/`. `RESTORE ORIGINAL` cleanly restores the unit to factory state.

---

## 🚀 Installation & Testing

### 1. Build & Prepare the SD Card

Using the root build script (builds all native binaries, Java patches, pins JAR checksums, and copies to SD):

```sh
# Build and stage files into build/sd/
STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh

# Or directly copy and verify onto the FAT32 SD card:
SD=/Volumes/SD32 STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh
```

The SD card root will contain:
`metainfo2.txt`, `Toolbox/`, `SD_CARD_README.txt`, and `SHA256SUMS-SD.txt`.

> [!NOTE]
> If your SD card already has an `MMI-Cockpit-Carplay/` folder with stock backups from your car, **keep it on the card**. `RESTORE ORIGINAL` requires the unit's stock backups to revert changes.

### 2. Update MIB Toolbox

1. Insert the SD card into slot 1 (SD1) of the MMI unit.
2. If MIB Toolbox is already on the unit: open Green Engineering Menu (GEM) -> **Toolbox -> Update Toolbox** to refresh scripts and menus.
3. If MIB Toolbox is not yet installed: use the MMI **Software Update (SWDL)** menu to install the package using `metainfo2.txt`.
4. Confirm that the `MMI-Cockpit-Carplay` menu appears in GEM.

### 3. Install & Start

In the GEM **MMI-Cockpit-Carplay** menu, execute the following steps in order:

1. **Disconnect iPhone / CarPlay** so no navigation video is streaming during installation.
2. Select **INSTALL**. Wait for completion (`INSTALL=PASS`, `reboot_required=YES`), then **fully reboot the head unit** (hold MMI knob + top right + nav toggle).
3. After reboot, enter GEM -> **MMI-Cockpit-Carplay -> START**. Wait for `START=PASS` and `reboot_required=YES`, then **fully reboot the head unit again**.
4. After the second reboot, connect your iPhone, start CarPlay, and launch navigation (Apple Maps / Google Maps). The Virtual Cockpit will show the CarPlay cluster video stream with the maneuver overlay.

### 4. Verification & Testing

- **Steering-wheel zoom:** While navigation is running on the cluster, turn the left steering-wheel roller to zoom the CarPlay map in and out.
- **Cluster map layout (marker position):** In GEM -> **MMI-Cockpit-Carplay -> Cluster map layout**, test the four layout presets, reconnecting the phone after each to check which centers the vehicle marker best for your cluster layout (Classic or Sport).
- **STATUS:** Open **MMI-Cockpit-Carplay -> STATUS** in GEM. It verifies `DIO_PRELOAD_ALTSCREEN`, `DIO_PRELOAD_RGI`, `RGI_*`, and live cluster display context (`81` = video + maneuver, `80` = maneuver on stock map, `74` = idle).

### 5. Restore Original Stock Configuration

1. Insert the SD card containing your `MMI-Cockpit-Carplay` backup directory.
2. In GEM, open **MMI-Cockpit-Carplay -> RESTORE ORIGINAL** (or `STORE LOGS + RESTORE` to collect diagnostics first).
3. Wait for `RESTORE=PASS` and `reboot_required=YES`, then fully reboot the head unit. All patches are uninstalled and stock configurations are restored.

---

## 📜 Licenses & Attribution

- **Upstream AltScreen Project:** Developed by [yuedizhibo](https://github.com/yuedizhibo) and [Lanye-z](https://github.com/Lanye-z) ([MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)). Original material licensed under [PolyForm Noncommercial 1.0.0](LICENSE). Noncommercial use only; do not resell.
- **Route Guidance Integration (RGI):** Developed by [luka-dev](https://github.com/luka-dev/mib2q-carplay-rgi).
- **MIB2 High Toolbox:** Upstream tools and GEM scripting by [jilleb](https://github.com/jilleb/mib2-toolbox) under [MIT License](LICENSE.TOOLBOX-MIT).
- **Mirror Runtime:** Independent license [LICENSE.MMI-MIRROR](Toolbox/carplay_alt_screen/mirror_display/release/LICENSE.MMI-MIRROR).
