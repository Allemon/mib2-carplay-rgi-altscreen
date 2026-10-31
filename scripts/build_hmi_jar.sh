#!/bin/bash
#
# Build the HMI JAR: upstream's V3.8 Fix1 release JAR with the classes of the Java sources the
# fork changes recompiled and replaced. Everything else in the JAR stays byte for byte.
#
#   RELEASE_ZIP=build/cache/V3.8Fix1.zip ./scripts/build_hmi_jar.sh
#   STOCK_JAR=~/Tools/jxe2jar/out/MU1329-base.jar     (default; your unit's converted lsd.jxe)
#
# Upstream does not publish its JAR build, so each run first proves the method: upstream's own
# version of every changed source (git ref ALTSCREEN_REF, default altscreen/main) is compiled the
# same way and must reproduce the release JAR's classes exactly. Then the fork's version is
# compiled and its classes replace those (and only those) entries.
#
# javac 1.8 (eclipse-temurin:8 in Docker), -source 1.3 -target 1.2 (class major 46, as the
# release), default debug info (source + lines), against the release JAR + the stock JAR + OSGi.
# Output: build/native/carplay_hook-basevideo3.jar
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$PROJECT_DIR/build/native"
WORK="$PROJECT_DIR/build/hmi-jar"
IMG=eclipse-temurin:8-jdk-jammy
ZIP=${RELEASE_ZIP:-$PROJECT_DIR/build/cache/V3.8Fix1.zip}
STOCK_JAR=${STOCK_JAR:-$HOME/Tools/jxe2jar/out/MU1329-base.jar}
OSGI_JAR=${OSGI_JAR:-$(dirname "$(dirname "$STOCK_JAR")")/libs/org.osgi.framework-1.10.0.jar}
REF=${ALTSCREEN_REF:-altscreen/main}
JAR_PATH=Toolbox/carplay_alt_screen/hmi/carplay_hook-basevideo3.jar
SRC_ROOT=Toolbox/carplay_alt_screen/hmi/src
# Java sources the fork changes (relative to $SRC_ROOT).
CHANGED="com/luka/carplay/cluster/ClusterLayerController.java"

[ -f "$ZIP" ] || { echo "ERROR: release zip $ZIP missing (scripts/build_sd.sh downloads it)"; exit 1; }
[ -f "$STOCK_JAR" ] || { echo "ERROR: stock HMI jar $STOCK_JAR missing; set STOCK_JAR"; exit 1; }
[ -f "$OSGI_JAR" ] || { echo "ERROR: $OSGI_JAR missing; set OSGI_JAR"; exit 1; }
git -C "$PROJECT_DIR" rev-parse --verify -q "$REF^{commit}" >/dev/null \
    || { echo "ERROR: git ref $REF missing (git fetch altscreen)"; exit 1; }

rm -rf "$WORK"; mkdir -p "$WORK/upstream" "$WORK/fork" "$WORK/release" "$WORK/cp" "$OUT"
unzip -q -o "$ZIP" "$JAR_PATH" -d "$WORK" && mv "$WORK/$JAR_PATH" "$WORK/release.jar"
(cd "$WORK/release" && unzip -q ../release.jar)
cp "$STOCK_JAR" "$WORK/cp/stock.jar"; cp "$OSGI_JAR" "$WORK/cp/osgi.jar"
for s in $CHANGED; do
    mkdir -p "$WORK/upstream/src/$(dirname "$s")" "$WORK/fork/src/$(dirname "$s")"
    git -C "$PROJECT_DIR" show "$REF:$SRC_ROOT/$s" > "$WORK/upstream/src/$s"
    cp "$PROJECT_DIR/$SRC_ROOT/$s" "$WORK/fork/src/$s"
done

echo "=== HMI JAR: javac in $IMG (stock $(basename "$STOCK_JAR")) ==="
docker run --rm --platform=linux/amd64 -v "$WORK":/w -e CHANGED="$CHANGED" "$IMG" bash -c '
    set -e
    for side in upstream fork; do
        mkdir -p /w/$side/classes
        (cd /w/$side/src && javac -nowarn -source 1.3 -target 1.2 -encoding UTF-8 \
            -cp /w/release.jar:/w/cp/stock.jar:/w/cp/osgi.jar -d /w/$side/classes $CHANGED)
    done' > "$WORK/javac.log" 2>&1 || { cat "$WORK/javac.log"; echo "ERROR: javac failed"; exit 1; }

python3 -I - "$WORK" "$OUT/carplay_hook-basevideo3.jar" $CHANGED <<'PY'
import os, sys, zipfile
work, out, sources = sys.argv[1], sys.argv[2], sys.argv[3:]
def classes(root, src):
    base = src[:-len(".java")]
    d = os.path.join(root, os.path.dirname(base))
    stem = os.path.basename(base)
    names = sorted(f for f in os.listdir(d) if f == stem + ".class" or (f.startswith(stem + "$") and f.endswith(".class")))
    return {os.path.dirname(base) + "/" + f: open(os.path.join(d, f), "rb").read() for f in names}
release = zipfile.ZipFile(os.path.join(work, "release.jar"))
names = release.namelist()
replace = {}
for src in sources:
    up = classes(os.path.join(work, "upstream", "classes"), src)
    fork = classes(os.path.join(work, "fork", "classes"), src)
    for name, data in up.items():
        if name not in names or release.read(name) != data:
            sys.exit("ERROR: upstream %s does not reproduce the release class %s" % (src, name))
    old = {n for n in names if n in up or (n.startswith(src[:-5] + "$") and n.endswith(".class"))}
    if old != set(up):
        sys.exit("ERROR: release JAR has other classes for %s: %s" % (src, sorted(old ^ set(up))))
    replace.update({n: None for n in up})
    replace.update(fork)
    print("REPRODUCED %s (%d classes); fork: %d classes" % (src, len(up), len(fork)))
tmp = out + ".tmp"
with zipfile.ZipFile(tmp, "w") as dst:
    for info in release.infolist():
        if info.filename in replace:
            data = replace.pop(info.filename)
            if data is None: continue
            info.compress_type = zipfile.ZIP_DEFLATED
            dst.writestr(info, data)
        else:
            dst.writestr(info, release.read(info.filename), compress_type=info.compress_type)
    for name, data in sorted(replace.items()):
        if data is not None: dst.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
os.replace(tmp, out)
PY
echo "  $OUT/carplay_hook-basevideo3.jar"
