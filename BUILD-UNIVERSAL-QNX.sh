#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec /bin/sh "$ROOT/Toolbox/carplay_alt_screen/build_source_snapshot.sh" universal "$ROOT/Toolbox/carplay_alt_screen/dev-build/universal"
