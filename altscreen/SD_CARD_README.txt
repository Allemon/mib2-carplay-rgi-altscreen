MMI Cockpit CarPlay AltScreen + RGI SD Package

1. Copy all contents of this package directly to the root of a FAT32 SD card.
   The card root should directly contain:
   metainfo2.txt, Toolbox/, SD_CARD_README.txt, and SHA256SUMS-SD.txt.
2. If your previous SD card already has an MMI-Cockpit-Carplay directory with stock
   backups, state, or logs, preserve it and copy it to the new card. RESTORE ORIGINAL
   requires the original backups created during the initial installation.
3. Unit already has MIB Toolbox: Run "Update Toolbox" in the Toolbox menu to refresh
   scripts and the Green Engineering Menu (GEM).
   Unit does not have MIB Toolbox: Install the menu and scripts via the MMI Software
   Update (SWDL) menu using metainfo2.txt.
4. Execution in GEM (MMI-Cockpit-Carplay menu):
   Disconnect iPhone -> INSTALL -> Full MMI Reboot -> START -> Full MMI Reboot ->
   Connect iPhone / CarPlay -> Launch navigation.
5. Supported on MHI2Q units (tested on Audi Q5 FY 2019, MHI2Q_ER_AUG22_P5092, MU 1329).
   - Display video is rendered at 1:1 aspect ratio with clean bottom crop (no distortion).
   - Watermarks are completely removed (transparent overlay).
   - Startup screen displays the Audi logo (logo.rgba) for ~2 seconds.
   - Steering-wheel roller zooms CarPlay map and native map simultaneously.
   - Four Cluster map layout presets available in GEM for car marker positioning.
6. To restore stock firmware configurations, run RESTORE ORIGINAL from the menu.
7. Runtime state flags are stored on the unit at /mnt/app/root/carplay-altscreen/state.
   Cold boots do not require the SD card to remain inserted once installed.
   The SD card holds stock backups and diagnostic logs; always retain your backup card.
8. Temporary runtime files are written to /tmp. Configuration file replacements are
   performed atomically with .carplay-stock backups maintained.

Display pipeline, watermark removal, aspect-ratio correction, steering-wheel zoom,
and route guidance integration have all been verified on-vehicle.
