#!/bin/sh
# Menu entry: Cluster map area: 1080 px for Amap/Baidu (not with the card).
TARGET="${0%/*}/cluster_area.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/cluster_area.sh" ]; then TARGET="$candidate/Toolbox/scripts/cluster_area.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" w1080
