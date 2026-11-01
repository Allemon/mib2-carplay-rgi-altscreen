#!/bin/sh
# Menu entry: Map ETA: off.
TARGET="${0%/*}/map_eta.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/map_eta.sh" ]; then TARGET="$candidate/Toolbox/scripts/map_eta.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" off
