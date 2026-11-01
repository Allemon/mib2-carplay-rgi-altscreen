#!/bin/sh
# MMI-Cockpit-Carplay fork: write the default settings into a freshly staged runtime state.
#   fork_defaults.sh <defaults file> <state dir>
# Called by INSTALL (altscreen_chain_test.sh, install_runtime_scripts) right after it created
# the new runtime's state/ directory, so a reinstall keeps the owner's choices instead of
# V3.8's. The GEM entries still change every setting afterwards. Each line is key=value
# (Toolbox/carplay_alt_screen/fork-defaults.txt explains them); a missing file, an unknown key
# or an invalid value is logged and skipped, never fails the install.
set -u
DEFAULTS=${1:-}
STATE=${2:-}
[ -n "$STATE" ] && [ -d "$STATE" ] && [ ! -L "$STATE" ] || { echo "FORK_DEFAULTS=SKIPPED reason=NO_STATE_DIR"; exit 0; }
[ -n "$DEFAULTS" ] && [ -f "$DEFAULTS" ] || { echo "FORK_DEFAULTS=SKIPPED reason=NO_DEFAULTS_FILE"; exit 0; }

put(){    # put <file> <value>
    printf '%s\n' "$2" > "$STATE/$1.new" && chmod 644 "$STATE/$1.new" && mv -f "$STATE/$1.new" "$STATE/$1"
}
flag(){   # flag <file> <on|off> -> present when on
    if [ "$2" = on ]; then put "$1" "$2"; else rm -f "$STATE/$1"; fi
}
in_range(){ case "$1" in ''|*[!0-9]*) return 1 ;; esac; [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]; }

applied=0; skipped=0
while IFS= read -r line || [ -n "$line" ]; do
    line=${line%%#*}
    line=$(printf '%s' "$line" | tr -d ' \t\r')
    [ -n "$line" ] || continue
    key=${line%%=*}; value=${line#*=}
    [ "$key" != "$line" ] || { echo "FORK_DEFAULT=SKIPPED line=$line reason=NO_VALUE"; skipped=$((skipped + 1)); continue; }
    ok=1
    case "$key" in
        card_position)
            case "$value" in left|top|right) put cluster-card-layout.txt "$value" ;; *) ok=0 ;; esac ;;
        card_small_view)
            case "$value" in
                keep) rm -f "$STATE/card-small-hide" "$STATE/card-small-behind" ;;
                behind) rm -f "$STATE/card-small-hide"; put card-small-behind behind ;;
                hide) rm -f "$STATE/card-small-behind"; put card-small-hide hide ;;
                *) ok=0 ;;
            esac ;;
        map_top_edge)
            if [ "$value" = 72 ]; then rm -f "$STATE/cluster-safe-top.txt"
            elif in_range "$value" 60 140; then put cluster-safe-top.txt "$value"; else ok=0; fi ;;
        map_bottom_edge)
            if [ "$value" = 450 ]; then rm -f "$STATE/cluster-safe-bottom.txt"
            elif in_range "$value" 330 450; then put cluster-safe-bottom.txt "$value"; else ok=0; fi ;;
        map_area_width)
            case "$value" in
                1440) rm -f "$STATE/cluster-view-width.txt" ;;
                1200|1140|1080) put cluster-view-width.txt "$value" ;;
                *) ok=0 ;;
            esac ;;
        map_eta_panel)
            case "$value" in on|off) flag map-eta-on "$value" ;; *) ok=0 ;; esac ;;
        rgi_backing)
            case "$value" in on) rm -f "$STATE/rgi-backing-off" ;; off) put rgi-backing-off off ;; *) ok=0 ;; esac ;;
        night_sync)
            case "$value" in on) rm -f "$STATE/night-sync-off" ;; off) put night-sync-off off ;; *) ok=0 ;; esac ;;
        cluster_ui_size)
            case "$value" in v38) rm -f "$STATE/cluster-physical-width.txt" ;; compact) put cluster-physical-width.txt 292 ;; *) ok=0 ;; esac ;;
        map_shift)
            case "$value" in off) rm -f "$STATE/cluster-shift.txt" ;; sporttest) put cluster-shift.txt "small_dx=-476" ;; *) ok=0 ;; esac ;;
        *) ok=0 ;;
    esac
    if [ "$ok" = 1 ]; then
        echo "FORK_DEFAULT=APPLIED $key=$value"; applied=$((applied + 1))
    else
        echo "FORK_DEFAULT=SKIPPED $key=$value reason=UNKNOWN_KEY_OR_VALUE"; skipped=$((skipped + 1))
    fi
done < "$DEFAULTS"
echo "FORK_DEFAULTS=DONE applied=$applied skipped=$skipped"
exit 0
