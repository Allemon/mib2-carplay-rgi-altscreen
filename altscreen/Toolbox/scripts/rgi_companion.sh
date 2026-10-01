#!/bin/sh
# mib2q-carplay-rgi companion for MMI-Cockpit-Carplay (AltScreen).
#
#   rgi_companion.sh install   called by INSTALL after the Java80 JAR is published
#   rgi_companion.sh remove    called by RESTORE ORIGINAL before the native restore
#
# install puts the RGI native half next to AltScreen:
#   /mnt/app/root/hooks/  libcarplay_hook.so maneuver_render flag_atlas.rgba
#                         carplay_startup.sh carplay_monitor.sh carplay_processes.sh
#                         carplay_cleanup.sh
# and wires it in:
#   smartphone_integrator.json  carplay child -> carplay_startup.sh (RGI wrapper), with the
#                               AltScreen preload kept in its envs (and passed again as
#                               CARPLAY_PRELOAD_EXTRA); the wrapper builds
#                               LD_PRELOAD=<altscreen>:libcarplay_hook.so for dio_manager
#   dio_manager.json            iAP2 route-guidance IDs 0x5200..0x5204
# remove deletes only the files above; both JSON files come back from the AltScreen
# ORIGINAL backup in the controller restore that follows.
#
# QNX 6.5 /bin/sh is pdksh: portable subset only, no GNU tools.
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:$PATH
export PATH

BASE="$0"
RESOLVED=$(command -v -- "$BASE" 2>/dev/null)
[ -n "$RESOLVED" ] || RESOLVED="$BASE"
SCRIPTDIR=$(cd -P -- "$(dirname -- "$RESOLVED")" 2>/dev/null && pwd -P)
[ -n "$SCRIPTDIR" ] || { echo "FAIL: cannot resolve companion directory"; exit 126; }

TESTING=${ALTSCREEN_CHAIN_TESTING:-0}
DEVICE_ROOT=""
if [ "$TESTING" = 1 ]; then
    DEVICE_ROOT=${ALTSCREEN_CHAIN_ROOT:-}
    case "$DEVICE_ROOT" in /tmp/*|/var/tmp/*) ;; *) echo "FAIL: invalid ALTSCREEN_CHAIN_ROOT"; exit 2 ;; esac
fi
p(){ printf '%s%s\n' "$DEVICE_ROOT" "$1"; }

ACTION=${1:-}
HOOKS=$(p /mnt/app/root/hooks)
CFG=$(p /mnt/system/etc/eso/production/smartphone_integrator.json)
DIO=$(p /mnt/system/etc/eso/production/dio_manager.json)
ALTS_PRELOAD=/mnt/app/root/carplay-altscreen/lib/libcarplay_altscreen.so
FILES="libcarplay_hook.so maneuver_render flag_atlas.rgba carplay_startup.sh
carplay_monitor.sh carplay_processes.sh carplay_cleanup.sh"

mount_app_rw(){ [ "$TESTING" = 1 ] || mount -uw /mnt/app; }
mount_app_ro(){ [ "$TESTING" = 1 ] || mount -ur /mnt/app; }
mount_system_rw(){ [ "$TESTING" = 1 ] || mount -uw /mnt/system; }
mount_system_ro(){ [ "$TESTING" = 1 ] || mount -ur /mnt/system; }
finish_mounts(){ sync >/dev/null 2>&1 || true; mount_app_ro >/dev/null 2>&1 || true; mount_system_ro >/dev/null 2>&1 || true; }
fail(){ echo "FAIL: $1"; finish_mounts; exit 1; }

mode_for(){
    case $1 in
        *.so|*.sh|maneuver_render) echo 755 ;;
        *) echo 644 ;;
    esac
}
# count occurrences of $2 in $1 (ksh-safe, no external tools)
count_char(){ n=0; rest=$1; while :; do case $rest in *"$2"*) rest=${rest#*"$2"}; n=$((n+1)) ;; *) break ;; esac; done; echo "$n"; }

find_volume(){
    if [ -n "${ALTSCREEN_SD_VOLUME:-}" ]; then echo "$ALTSCREEN_SD_VOLUME"; return 0; fi
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -s "$candidate/Toolbox/carplay_alt_screen/rgi/libcarplay_hook.so" ]; then
            echo "$candidate"; return 0
        fi
    done
    return 1
}

# ---- smartphone_integrator.json: replace the "carplay" child by path ----------
# Same line-based editor as the mib2q-carplay-rgi M.I.B. installer; $1 = fragment, $2 = out.
replace_child(){
    frag=$1; out=$2
    : > "$out" || return 1
    state=copy; hits=0; depth=0
    while IFS= read -r line || [ -n "$line" ]; do
        if [ "$state" = copy ]; then
            case $line in
                *'"carplay"'*:*)
                    set -f; set -- $line; set +f
                    compact=; for part in "$@"; do compact=$compact$part; done
                    [ "$compact" = '"carplay":{' ] || { echo "unsupported carplay layout"; return 1; }
                    hits=$((hits+1)); state=skip; depth=0 ;;
                *) printf '%s\n' "$line" >> "$out"; continue ;;
            esac
        fi
        opens=$(count_char "$line" '{'); closes=$(count_char "$line" '}')
        depth=$((depth + opens - closes))
        if [ "$depth" -le 0 ]; then
            printf '        "carplay": ' >> "$out"
            cat "$frag" >> "$out"
            printf '%s\n' "${line##*\}}" >> "$out"       # keep the trailing comma
            state=copy
        fi
    done < "$CFG"
    [ "$hits" = 1 ] && [ "$state" = copy ] || { echo "expected one carplay child (hits=$hits)"; return 1; }
    o=$(grep -c '"exec"' "$CFG"); n=$(grep -c '"exec"' "$out")
    [ "$o" = "$n" ] || { echo "SI child count changed ($o->$n)"; return 1; }
}

patch_json(){
    awkf=$1; frag=$2
    [ -f "$CFG" ] || { echo "no SI config at $CFG"; return 1; }
    t1=$CFG.rgi-child.$$; t2=$CFG.rgi-new.$$
    replace_child "$frag" "$t1" || { rm -f "$t1"; return 1; }
    # The RGI fragment carries no LD_PRELOAD; re-insert the AltScreen one exactly as its
    # installer does, so START/STATUS still find the universal preload armed.
    awk -v hook="$ALTS_PRELOAD" -v insert_if_absent=1 -f "$awkf" "$t1" > "$t2" || { rm -f "$t1" "$t2"; echo "AltScreen preload insertion failed"; return 1; }
    rm -f "$t1"
    awk -v validate=1 -f "$awkf" "$t2" >/dev/null || { rm -f "$t2"; echo "patched SI json failed validation"; return 1; }
    awk -v query="$ALTS_PRELOAD" -f "$awkf" "$t2" >/dev/null || { rm -f "$t2"; echo "AltScreen preload missing after patch"; return 1; }
    grep -q '"exec": "carplay_startup.sh"' "$t2" || { rm -f "$t2"; echo "RGI wrapper missing after patch"; return 1; }
    # The wrapper takes the AltScreen preload from here: a preload already loaded into
    # the wrapper shell cannot be relied on to survive in LD_PRELOAD itself.
    grep -q "\"CARPLAY_PRELOAD_EXTRA=$ALTS_PRELOAD\"" "$t2" || { rm -f "$t2"; echo "CARPLAY_PRELOAD_EXTRA missing after patch"; return 1; }
    chmod 644 "$t2" && mv -f "$t2" "$CFG" || { rm -f "$t2"; return 1; }
    echo "RGI_SI_CHILD=INSTALLED exec=/mnt/app/root/hooks/carplay_startup.sh preload=$ALTS_PRELOAD+libcarplay_hook.so"
}

# ---- dio_manager.json: register the route-guidance message IDs ----------------
add_ids(){   # $1 = line holding one list, rest = IDs; prints the new line
    l=$1; shift
    for id in "$@"; do
        case $l in *"\"$id\""*) continue ;; esac
        head=${l%%]*}; tail=${l#*]}
        case $head in *'[') l=$head'"'$id'"]'$tail ;; *) l=$head', "'$id'"]'$tail ;; esac
    done
    printf '%s\n' "$l"
}
patch_dio(){
    [ -f "$DIO" ] || { echo "no $DIO"; return 1; }
    tmp=$DIO.rgi-new.$$; : > "$tmp" || return 1
    sent=0; recv=0; changed=0
    while IFS= read -r line || [ -n "$line" ]; do
        case $line in
            *'##'*) new=$line ;;
            *'"MessagesSentByAccessory":['*']'*)
                sent=$((sent+1)); new=$(add_ids "$line" 0x5200 0x5203) ;;
            *'"MessagesReceivedFromDevice":['*']'*)
                recv=$((recv+1)); new=$(add_ids "$line" 0x5201 0x5202 0x5204) ;;
            *) new=$line ;;
        esac
        [ "$new" = "$line" ] || { line=$new; changed=1; }
        printf '%s\n' "$line" >> "$tmp"
    done < "$DIO"
    if [ "$sent" != 1 ] || [ "$recv" != 1 ]; then
        rm -f "$tmp"; echo "dio_manager.json lists sent=$sent recv=$recv (expected 1/1)"; return 1
    fi
    n=0; for id in 0x5200 0x5201 0x5202 0x5203 0x5204; do grep -q "\"$id\"" "$tmp" && n=$((n+1)); done
    [ "$n" = 5 ] || { rm -f "$tmp"; echo "dio_manager.json has $n/5 IDs after edit"; return 1; }
    if [ "$changed" = 0 ]; then rm -f "$tmp"; echo "RGI_DIO_IDS=ALREADY_REGISTERED"; return 0; fi
    chmod 644 "$tmp" && mv -f "$tmp" "$DIO" || { rm -f "$tmp"; return 1; }
    echo "RGI_DIO_IDS=REGISTERED 0x5200..0x5204"
}

remove_files(){
    for name in $FILES; do
        rm -f "$HOOKS/$name" "$HOOKS/$name.rgi-new."* 2>/dev/null
        [ ! -e "$HOOKS/$name" ] || return 1
    done
    rm -f "$HOOKS/cluster_ui.url" "$HOOKS/cluster_fps" "$HOOKS/cluster_shift.cfg" "$HOOKS/cluster_viewarea.cfg"   # GEM cluster choices
    rmdir "$HOOKS" 2>/dev/null || true
}

cmd_install(){
    VOLUME=$(find_volume) || { echo "FAIL: no SD card with Toolbox/carplay_alt_screen/rgi"; exit 1; }
    SRC=$VOLUME/Toolbox/carplay_alt_screen/rgi
    AWKF=$VOLUME/Toolbox/scripts/altscreen_preload.awk
    [ -s "$AWKF" ] || { echo "FAIL: altscreen_preload.awk missing: $AWKF"; exit 1; }
    missing=
    for name in $FILES carplay_child.json; do [ -s "$SRC/$name" ] || missing="$missing $name"; done
    [ -z "$missing" ] || { echo "FAIL: RGI payload incomplete in $SRC:$missing"; exit 1; }
    for name in carplay_startup.sh carplay_monitor.sh carplay_processes.sh carplay_cleanup.sh; do
        sh -n "$SRC/$name" || { echo "FAIL: RGI script syntax: $name"; exit 1; }
    done

    mount_app_rw || fail "cannot mount /mnt/app writable"
    mount_system_rw || fail "cannot mount /mnt/system writable"
    mkdir -p "$HOOKS" || fail "cannot create $HOOKS"
    for name in $FILES; do
        dst=$HOOKS/$name; tmp=$dst.rgi-new.$$
        cp "$SRC/$name" "$tmp" && cmp -s "$SRC/$name" "$tmp" && chmod "$(mode_for "$name")" "$tmp" &&
            mv -f "$tmp" "$dst" || { rm -f "$tmp"; remove_files; fail "cannot publish $dst"; }
    done
    echo "RGI_NATIVE=INSTALLED dir=/mnt/app/root/hooks"
    patch_json "$AWKF" "$SRC/carplay_child.json" || { remove_files; fail "smartphone_integrator.json not patched"; }
    patch_dio || { remove_files; fail "dio_manager.json not patched"; }
    finish_mounts
    echo "RGI_COMPANION=PASS"
}

cmd_remove(){
    present=0
    for name in $FILES; do [ -e "$HOOKS/$name" ] && present=1; done
    if [ "$present" = 0 ]; then echo "RGI_NATIVE=ABSENT"; return 0; fi
    # Best effort: a live renderer is reaped by the reboot RESTORE requires anyway.
    mount_app_rw || fail "cannot mount /mnt/app writable"
    remove_files || fail "cannot remove RGI files from /mnt/app/root/hooks"
    finish_mounts
    echo "RGI_NATIVE=REMOVED"
}

case "$ACTION" in
    install) cmd_install ;;
    remove)  cmd_remove ;;
    *) echo "usage: rgi_companion.sh install|remove"; exit 2 ;;
esac
