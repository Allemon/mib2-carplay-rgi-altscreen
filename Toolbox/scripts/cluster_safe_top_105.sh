#!/bin/sh
# Menu entry: Map top edge: 105.
TARGET="${0%/*}/cluster_safe_top.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/cluster_safe_top.sh" ]; then TARGET="$candidate/Toolbox/scripts/cluster_safe_top.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" 105
