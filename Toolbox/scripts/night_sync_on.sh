#!/bin/sh
# Menu entry: Day/night sync: on.
TARGET="${0%/*}/night_sync.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/night_sync.sh" ]; then TARGET="$candidate/Toolbox/scripts/night_sync.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" on
