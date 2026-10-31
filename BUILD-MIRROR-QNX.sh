#!/bin/sh
# Convenience entry point for GitHub Download ZIP users.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec /bin/sh "$ROOT/Toolbox/carplay_alt_screen/mirror_display/build_qnx.sh"
