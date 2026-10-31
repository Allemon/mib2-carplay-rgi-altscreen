#!/bin/sh
# Menu entry: Card in small view: keep.
TARGET="${0%/*}/card_small.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/card_small.sh" ]; then TARGET="$candidate/Toolbox/scripts/card_small.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" keep
