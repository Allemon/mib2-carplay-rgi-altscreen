#!/bin/sh
# Menu entry: Cluster map shift: Sport test (SMALL view -476 px on a Classic car).
TARGET="${0%/*}/cluster_shift.sh"
if [ ! -f "$TARGET" ]; then
    for candidate in /net/mmx/fs/sda0 /net/mmx/fs/sda1 /net/mmx/fs/sdb0 /net/mmx/fs/sdb1 /fs/sda0 /fs/sda1 /fs/sdb0 /fs/sdb1; do
        if [ -f "$candidate/Toolbox/scripts/cluster_shift.sh" ]; then TARGET="$candidate/Toolbox/scripts/cluster_shift.sh"; break; fi
    done
fi
exec /bin/sh "$TARGET" sporttest
