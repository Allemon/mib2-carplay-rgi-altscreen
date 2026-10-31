#!/bin/bash
#
# Build the complete MMI-Cockpit-Carplay SD card: MIB2 Toolbox + CarPlay AltScreen V3.8 Fix1
# + this fork's additions (cluster map area / shift, H.264 dump, verbose Java logs).
#
#   ./scripts/build_sd.sh                      # build the two changed binaries, stage build/sd/
#   SKIP_BUILD=1 ./scripts/build_sd.sh         # reuse build/native/
#   SD=/Volumes/SD32 ./scripts/build_sd.sh     # also write it onto the card
#   RELEASE_ZIP=~/Downloads/V3.8Fix1.zip ./scripts/build_sd.sh   # no download
#   UPSTREAM_MIRROR=1 ./scripts/build_sd.sh    # no QNX toolchain: upstream's map display, so
#                                              # "Cluster map shift" has no effect on this card
#
# Inputs:  sd_base/                 MIB2 Toolbox tree of the V3.8 Fix1 release card, without
#                                   the AltScreen overlay
#          Toolbox/ (this repo)     the overlay: scripts, GEM menu, licenses, BUILD_INFO, ...
#          SHA256SUMS-SD.txt        upstream's overlay list; also pins the binaries below
#          build/native/            libcarplay_altscreen.so, carplay-alt111-mirror-display
#                                   (scripts/build_native.sh), carplay_hook-basevideo3.jar
#                                   (scripts/build_hmi_jar.sh: the release JAR with the fork's
#                                   changed classes; needs your stock HMI jar, STOCK_JAR=)
#          V3.8Fix1.zip (release)   libcarplay_rgi_meta.so and maneuver_render, which the fork
#                                   does not change; each is checked against its pinned hash
# Output:  build/sd/                copy its contents onto the SD root
#
# The card's MMI-Cockpit-Carplay/ (stock backups, logs, state) is never part of the output and
# never touched by SD=: RESTORE ORIGINAL needs the backups on that card.
set -euo pipefail
export COPYFILE_DISABLE=1   # macOS tar: no AppleDouble ._ files on the FAT card

[ "$#" -eq 0 ] || { echo "usage: [SKIP_BUILD=1] [SD=<card>] [RELEASE_ZIP=<zip>] [UPSTREAM_MIRROR=1] ./scripts/build_sd.sh"; exit 2; }

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$PROJECT_DIR/build/sd"
NATIVE="$PROJECT_DIR/build/native"
CACHE="$PROJECT_DIR/build/cache"
SUMS="$PROJECT_DIR/SHA256SUMS-SD.txt"
RELEASE_URL=https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen/releases/download/v3.8-fix1/V3.8Fix1.zip
RELEASE_SHA256=ed56797fab604e402754143127042e50446169f3910eb561ec87a04ba82dd288
ALT=Toolbox/carplay_alt_screen
# Built here from this repo's sources.
BUILT_HOOK=$ALT/universal/libcarplay_altscreen.so
BUILT_MIRROR=$ALT/mirror_display/release/carplay-alt111-mirror-display
BUILT_JAR=$ALT/hmi/carplay_hook-basevideo3.jar
# Unchanged upstream binaries, taken from the release.
FROM_RELEASE="$ALT/rgi_meta/libcarplay_rgi_meta.so $ALT/rgi_renderer/release/maneuver_render"
# Files the fork adds to upstream's overlay list.
FORK_FILES="
Toolbox/scripts/cluster_area.sh
Toolbox/scripts/cluster_area_full.sh
Toolbox/scripts/cluster_area_w1200.sh
Toolbox/scripts/cluster_area_w1140.sh
Toolbox/scripts/cluster_area_w1080.sh
Toolbox/scripts/cluster_shift.sh
Toolbox/scripts/cluster_shift_off.sh
Toolbox/scripts/cluster_shift_sporttest.sh
Toolbox/scripts/dump_cluster_h264.sh
Toolbox/scripts/rgi_verbose.sh
Toolbox/scripts/rgi_verbose_on.sh
Toolbox/scripts/rgi_verbose_off.sh
Toolbox/scripts/repair_after_hybrid.sh
Toolbox/scripts/rgi_backing.sh
Toolbox/scripts/rgi_backing_on.sh
Toolbox/scripts/rgi_backing_off.sh"
NATIVE_FILES="$NATIVE/libcarplay_altscreen.so $NATIVE/carplay-alt111-mirror-display $NATIVE/carplay_hook-basevideo3.jar"
NATIVE_WHAT=all
if [ "${UPSTREAM_MIRROR:-0}" = 1 ]; then
    FROM_RELEASE="$FROM_RELEASE $BUILT_MIRROR"
    NATIVE_FILES="$NATIVE/libcarplay_altscreen.so $NATIVE/carplay_hook-basevideo3.jar"
    NATIVE_WHAT=hook
    echo "WARN: UPSTREAM_MIRROR=1 - the card gets upstream's map display; 'Cluster map shift' does nothing on it."
fi

if command -v sha256sum >/dev/null 2>&1; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
sha_of() { $SHA "$1" | cut -d' ' -f1; }
pinned() { awk -v p="$1" '$2 == p { print $1 }' "$SUMS"; }

[ -d "$PROJECT_DIR/sd_base/Toolbox" ] || { echo "ERROR: sd_base/ is not an SD tree"; exit 1; }

# The release card, for the binaries the fork does not change.
ZIP=${RELEASE_ZIP:-$CACHE/V3.8Fix1.zip}
if [ ! -f "$ZIP" ]; then
    mkdir -p "$CACHE"
    echo "=== downloading $RELEASE_URL ==="
    curl -fL --retry 3 -o "$ZIP.part" "$RELEASE_URL" && mv -f "$ZIP.part" "$ZIP"
fi
[ "$(sha_of "$ZIP")" = "$RELEASE_SHA256" ] || { echo "ERROR: $ZIP is not the V3.8 Fix1 release (sha256)"; exit 1; }

if [ "${SKIP_BUILD:-0}" != 1 ]; then
    bash "$PROJECT_DIR/scripts/build_native.sh" "$NATIVE_WHAT"
    RELEASE_ZIP="$ZIP" bash "$PROJECT_DIR/scripts/build_hmi_jar.sh"
fi
for f in $NATIVE_FILES; do
    [ -s "$f" ] || { echo "ERROR: missing $f - run ./scripts/build_native.sh / build_hmi_jar.sh"; exit 1; }
done
REL="$CACHE/release"
rm -rf "$REL"; mkdir -p "$REL"
for f in $FROM_RELEASE; do
    unzip -q -o "$ZIP" "$f" -d "$REL"
    [ "$(sha_of "$REL/$f")" = "$(pinned "$f")" ] || { echo "ERROR: release $f does not match SHA256SUMS-SD.txt"; exit 1; }
done

echo "=== MMI-Cockpit-Carplay SD (V3.8 Fix1 + fork) -> $OUT ==="
rm -rf "$OUT"; mkdir -p "$OUT"
(cd "$PROJECT_DIR/sd_base" && tar --exclude=.DS_Store --exclude='._*' -cf - .) | (cd "$OUT" && tar -xf -)

overlay=$(awk '{ print $2 }' "$SUMS"; printf '%s\n' $FORK_FILES)
for f in $overlay; do
    mkdir -p "$OUT/$(dirname "$f")"
    case " $FROM_RELEASE " in *" $f "*) cp "$REL/$f" "$OUT/$f"; continue ;; esac
    case "$f" in
        "$BUILT_HOOK") cp "$NATIVE/libcarplay_altscreen.so" "$OUT/$f" ;;
        "$BUILT_MIRROR") cp "$NATIVE/carplay-alt111-mirror-display" "$OUT/$f" ;;
        "$BUILT_JAR") cp "$NATIVE/carplay_hook-basevideo3.jar" "$OUT/$f" ;;
        *) [ -f "$PROJECT_DIR/$f" ] || { echo "ERROR: overlay file missing in the repo: $f"; exit 1; }
           cp "$PROJECT_DIR/$f" "$OUT/$f" ;;
    esac
done
chmod 755 "$OUT"/Toolbox/scripts/*.sh "$OUT/$BUILT_MIRROR" "$OUT/$ALT/rgi_renderer/release/maneuver_render" \
          "$OUT"/$ALT/mirror_display/release/*.sh

# Keep the informational checksums next to the rebuilt binaries true to what ships.
MIRROR_SUMS="$OUT/$ALT/mirror_display/release/SHA256SUMS"
mirror_sum=$(sha_of "$OUT/$BUILT_MIRROR")
sed -e "s/^[0-9a-f]\{64\}  carplay-alt111-mirror-display\$/$mirror_sum  carplay-alt111-mirror-display/" \
    "$MIRROR_SUMS" > "$MIRROR_SUMS.tmp" && mv -f "$MIRROR_SUMS.tmp" "$MIRROR_SUMS"
(cd "$(dirname "$MIRROR_SUMS")" && $SHA -c SHA256SUMS >/dev/null) \
    || { echo "ERROR: mirror SHA256SUMS does not match the shipped files"; exit 1; }
hook_sum=$(sha_of "$OUT/$BUILT_HOOK")
HMI_INFO="$OUT/$ALT/hmi/BUILD_INFO.txt"
grep -q '^universal_runtime_sha256=' "$HMI_INFO" || { echo "ERROR: $HMI_INFO lost universal_runtime_sha256"; exit 1; }
sed -e "s/^universal_runtime_sha256=.*/universal_runtime_sha256=$hook_sum/" "$HMI_INFO" > "$HMI_INFO.tmp"
mv -f "$HMI_INFO.tmp" "$HMI_INFO"
# The installer pins the JAR by the size/cksum in BUILD_INFO.txt: pin the one that ships.
set -- $(cksum < "$OUT/$BUILT_JAR")
JAR_CKSUM=$1 JAR_SIZE=$2 JAR_SHA=$(sha_of "$OUT/$BUILT_JAR")
for key in jar_size jar_cksum jar_sha256; do
    [ "$(grep -c "^$key=" "$HMI_INFO")" = 1 ] || { echo "ERROR: $HMI_INFO needs exactly one $key="; exit 1; }
done
sed -e "s/^jar_size=.*/jar_size=$JAR_SIZE/" -e "s/^jar_cksum=.*/jar_cksum=$JAR_CKSUM/" \
    -e "s/^jar_sha256=.*/jar_sha256=$JAR_SHA/" "$HMI_INFO" > "$HMI_INFO.tmp"
mv -f "$HMI_INFO.tmp" "$HMI_INFO"

# Everything the head unit runs must parse as sh (QNX /bin/sh is pdksh).
for f in "$OUT"/Toolbox/scripts/*.sh "$OUT"/$ALT/mirror_display/release/*.sh; do
    sh -n "$f" || { echo "ERROR: shell syntax: $f"; exit 1; }
done
# Every GEM entry must name a script that ships.
grep -o '/eso/hmi/engdefs/scripts/mqb/[A-Za-z0-9_]*\.sh' "$OUT/Toolbox/GEM/mqb-carplayAltScreen.esd" | sort -u |
while read -r s; do
    [ -f "$OUT/Toolbox/scripts/${s##*/}" ] || { echo "ERROR: GEM names a missing script: ${s##*/}"; exit 1; }
done

# SD integrity list (sha256sum -c SHA256SUMS-SD.txt on the card).
( cd "$OUT"
  for f in $overlay; do $SHA "$f"; done > SHA256SUMS-SD.txt
  $SHA -c SHA256SUMS-SD.txt >/dev/null )

echo "  hook:   $BUILT_HOOK sha256=$hook_sum"
echo "  mirror: $BUILT_MIRROR sha256=$mirror_sum"
echo "  jar:    $BUILT_JAR size=$JAR_SIZE cksum=$JAR_CKSUM"
echo "  from release: $(echo $FROM_RELEASE | wc -w | tr -d ' ') binaries, pinned"
echo "  sums:   SHA256SUMS-SD.txt ($(wc -l < "$OUT/SHA256SUMS-SD.txt" | tr -d ' ') files, verified)"

if [ -n "${SD:-}" ]; then
    [ -d "$SD" ] || { echo "ERROR: SD=$SD is not a directory"; exit 1; }
    [ -d "$SD/MMI-Cockpit-Carplay" ] || echo "WARN: $SD has no MMI-Cockpit-Carplay/ (no stock backups on this card yet)"
    if [ -d "$SD/Toolbox/carplay_alt_screen/rgi" ]; then
        echo "ERROR: $SD holds the old luka+AltScreen hybrid (Toolbox/carplay_alt_screen/rgi/)."
        echo "  Run RESTORE ORIGINAL with that card first and reboot (see README, 'Coming from the hybrid'),"
        echo "  then remove Toolbox/carplay_alt_screen/rgi/ from the card and run this again."
        exit 1
    fi
    echo "=== sync -> $SD (MMI-Cockpit-Carplay/ untouched) ==="
    # The AltScreen payload is entirely generated: replace it so no stale file of an older
    # build stays behind (V3.8 no longer uses logo.rgba / watermark.rgba, for one).
    rm -rf "$SD/$ALT"
    STAMP="$PROJECT_DIR/build/.sd-sync-stamp"; : > "$STAMP"
    (cd "$OUT" && tar -cf - .) | (cd "$SD" && tar -xf -)
    # macOS stores new files' xattrs as AppleDouble ._* on FAT; drop the ones this copy made.
    find "$SD" -path "$SD/MMI-Cockpit-Carplay" -prune -o -type f -name '._*' -newer "$STAMP" -exec rm -f {} + 2>/dev/null || true
    rm -f "$STAMP"
    sync
    (cd "$SD" && $SHA -c SHA256SUMS-SD.txt >/dev/null) && echo "  card verified against SHA256SUMS-SD.txt"
fi
echo "Done. On the car: MQBCoding -> Update Toolbox, then MMI-Cockpit-Carplay -> one INSTALL entry, reboot."
