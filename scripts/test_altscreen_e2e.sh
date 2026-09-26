#!/bin/bash
#
# AltScreen + RGI SD package, end to end, in the AltScreen scripts' own testing mode:
# INSTALL -> START -> STATUS -> RESTORE ORIGINAL against a fake head-unit root.
#
#   FIXTURE=<card>/MMI-Cockpit-Carplay/backup ./scripts/test_altscreen_e2e.sh
#
# FIXTURE is an AltScreen stock backup from a real unit (ORIGINAL/files, firewall-original,
# boot-diagnostics); it holds that unit's dio_manager, libairplay, JSON configs and
# startup.sh, so it stays outside the repo. The package is build/sd (./scripts/build_sd.sh).
#
# Runs in Linux with mksh as /bin/sh: QNX /bin/sh is pdksh (dash rejects the stock
# startup.sh), and on macOS the /tmp symlink breaks AltScreen's runtime-forward check.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SD_DIR="${SD_DIR:-$PROJECT_DIR/build/sd}"
FIXTURE="${FIXTURE:?set FIXTURE=<card>/MMI-Cockpit-Carplay/backup}"
[ -f "$SD_DIR/SHA256SUMS-SD.txt" ] || { echo "ERROR: no package in $SD_DIR; run ./scripts/build_sd.sh"; exit 1; }
[ -d "$FIXTURE/ORIGINAL/files" ] || { echo "ERROR: $FIXTURE has no ORIGINAL/files"; exit 1; }

docker run --rm -v "$SD_DIR":/sd:ro -v "$FIXTURE":/fixture:ro eclipse-temurin:8-jdk-jammy bash -c '
set -u
apt-get update -qq >/dev/null 2>&1 && apt-get install -y -qq mksh >/dev/null 2>&1 || { echo "ERROR: cannot install mksh"; exit 1; }
ln -sf /bin/mksh /bin/sh
F=/fixture/ORIGINAL/files; VOL=/tmp/vol; ROOT=/tmp/root
mkdir -p $VOL; (cd /sd && tar -cf - .) | (cd $VOL && tar -xf -)
mkdir -p $ROOT/eso/bin/apps $ROOT/eso/lib $ROOT/armle/usr/lib $ROOT/mnt/system/etc/eso/production \
         $ROOT/mnt/system/etc/boot $ROOT/mnt/app/root $ROOT/mnt/app/eso/hmi/lsd/jars $ROOT/dev/shmem $ROOT/tmp
cp $F/_eso_bin_apps_dio_manager $ROOT/eso/bin/apps/dio_manager
cp $F/_eso_lib_libairplay.so $ROOT/eso/lib/libairplay.so
cp $F/_armle_usr_lib_libNmeBaseClasses.so $ROOT/armle/usr/lib/libNmeBaseClasses.so
P=$ROOT/mnt/system/etc/eso/production
cp $F/_mnt_system_etc_eso_production_smartphone_integrator.json $P/smartphone_integrator.json
cp $F/_mnt_system_etc_eso_production_dio_manager.json $P/dio_manager.json
cp /fixture/firewall-original/pf.conf $ROOT/mnt/system/etc/pf.conf
cp /fixture/boot-diagnostics/startup.sh $ROOT/mnt/system/etc/boot/startup.sh
echo "Current train = MHI2Q_ER_AUG22_FIXTURE" > $ROOT/dev/shmem/version.txt
cp $P/dio_manager.json /tmp/dio.orig; cp $P/smartphone_integrator.json /tmp/si.orig
export ALTSCREEN_CHAIN_TESTING=1 ALTSCREEN_CHAIN_ROOT=$ROOT ALTSCREEN_CHAIN_VOLUME=$VOL
cd $VOL/Toolbox/scripts
fails=0
need(){ grep -q "$2" /tmp/$1.log && echo "  ok   $1: $2" || { echo "  FAIL $1: missing $2"; fails=$((fails+1)); }; }
run(){ timeout 180 /bin/sh "$2" > /tmp/$1.log 2>&1; echo "== $1 rc=$?"; }

run install ./install_mmi_cockpit_carplay_rx.sh
need install "INSTALL=PASS integrated=.*+RGI"
need install "RGI_COMPANION=PASS"
grep -q "\"CARPLAY_PRELOAD_EXTRA=/mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so\"" $P/smartphone_integrator.json \
    && echo "  ok   install: CARPLAY_PRELOAD_EXTRA in carplay child" || { echo "  FAIL install: CARPLAY_PRELOAD_EXTRA"; fails=$((fails+1)); }
H=$ROOT/mnt/app/root/hooks
sed -n "/^INHERITED_PRELOAD=/,/^echo \"\\[startup\\] preload/p" $H/carplay_startup.sh > /tmp/pre.sh
got=$(env -u LD_PRELOAD CARPLAY_PRELOAD_EXTRA=/mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so H=/mnt/app/root/hooks WLOG=/dev/null /bin/sh -c ". /tmp/pre.sh; echo \$LD_PRELOAD")
[ "$got" = /mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so:/mnt/app/root/hooks/libcarplay_hook.so ] \
    && echo "  ok   wrapper: dio_manager preload $got" || { echo "  FAIL wrapper preload: $got"; fails=$((fails+1)); }

run start ./start_mmi_cockpit_carplay_rx_test.sh   # the ARM mirror binary cannot run here
need start "START=PASS"

run status ./status_mmi_cockpit_carplay_test.sh
need status "HMI_CONTROL_PLANE=PASS"
need status "UNIVERSAL_PRELOAD_CONFIG=ARMED"
need status "RGI_NATIVE=INSTALLED"
need status "RGI_SI_CHILD=WRAPPER"
need status "RGI_DIO_IDS=5/5"

run restore ./stop_mmi_cockpit_carplay_test.sh
need restore "RGI_NATIVE=REMOVED"
need restore "RESTORE=PASS integrated"
[ ! -e $H ] && [ ! -e $ROOT/mnt/app/eso/hmi/lsd/jars/carplay_hook.jar ] \
    && echo "  ok   restore: hooks and JAR removed" || { echo "  FAIL restore: files left"; fails=$((fails+1)); }
cmp -s /tmp/dio.orig $P/dio_manager.json && cmp -s /tmp/si.orig $P/smartphone_integrator.json \
    && echo "  ok   restore: both JSON configs byte-identical to ORIGINAL" || { echo "  FAIL restore: JSON differs"; fails=$((fails+1)); }

if [ "$fails" = 0 ]; then echo "AltScreen+RGI e2e: INSTALL/START/STATUS/RESTORE PASS"; else
    echo "AltScreen+RGI e2e: $fails FAILED"; for l in /tmp/*.log; do echo "----- $l"; tail -30 "$l"; done; exit 1; fi
'
