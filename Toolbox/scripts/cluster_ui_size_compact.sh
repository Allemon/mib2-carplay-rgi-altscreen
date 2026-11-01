#!/bin/sh
# Menu entry: Cluster UI size: compact (292 mm).
TARGET="${0%/*}/cluster_ui_size.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/cluster_ui_size.sh" ]; then TARGET="$candidate/Toolbox/scripts/cluster_ui_size.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" compact
