#!/bin/bash
#
# Build the canonical CarPlay Java patch — in Docker (no host JDK required).
#
#   ./scripts/build_java.sh
#
# Input:  java_patch/   +   ../../Tools/jxe2jar   (stock jar + OSGi libs)
# Output: build/carplay_hook.jar
#
# Compiles against MU1316-final.jar + OSGi, target 1.4 (jclfoun11 = Foundation 1.1),
# inside a pinned JDK 8 container so the build does not depend on a host JVM.
#
# NOTE: MU1316-final.jar is the author's own decompiled stock HMI jar
# (../../Tools/jxe2jar/out/). For another MU train, convert that unit's lsd.jxe
# into the same out/ folder and name it:  STOCK_JAR=MU1329-base.jar ./scripts/build_java.sh
# Older trains whose stock API differs in a few classes (MU1003, CN K1004) are detected from the
# jar and built through tools/java_variant.py; JAVA_VARIANT=<name>|none overrides the detection.
set -e

[ "$#" -eq 0 ] || { echo "usage: ./scripts/build_java.sh"; exit 2; }

IMG=eclipse-temurin:8-jdk-jammy
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS_DIR="$(cd "$PROJECT_DIR/../../Tools/jxe2jar" && pwd)"

STOCK_JAR_NAME=${STOCK_JAR:-MU1316-final.jar}
STOCK_JAR="$TOOLS_DIR/out/$STOCK_JAR_NAME"
[ -f "$STOCK_JAR" ] || { echo "ERROR: $STOCK_JAR not found"; exit 1; }
[ -d "$PROJECT_DIR/java_patch" ] || { echo "ERROR: java_patch/ not found"; exit 1; }

# Reproducible build identity is computed on the host (git lives here), passed into the container.
BUILD_ID_RAW=${CARPLAY_BUILD_ID:-$(git -C "$PROJECT_DIR" describe --always --dirty 2>/dev/null || echo unknown)}
BUILD_ID=$(printf '%s' "$BUILD_ID_RAW" | tr -cd 'A-Za-z0-9._-')

# Stock API variant: edited copies of the affected sources go to build/java_variant/.
JAVA_VARIANT=${JAVA_VARIANT:-$(python3 "$PROJECT_DIR/tools/java_variant.py" --detect "$STOCK_JAR")}
rm -rf "$PROJECT_DIR/build/java_variant"
case "$JAVA_VARIANT" in
    none) ;;
    unknown) echo "ERROR: $STOCK_JAR_NAME matches no known HMI API (see tools/java_variant.py)"; exit 1 ;;
    *) python3 "$PROJECT_DIR/tools/java_variant.py" "$JAVA_VARIANT" "$PROJECT_DIR/java_patch" \
           "$PROJECT_DIR/build/java_variant" || exit 1
       BUILD_ID="$BUILD_ID-$JAVA_VARIANT" ;;
esac

echo "=== CarPlay Java Patch Build (Docker $IMG, stock $STOCK_JAR_NAME, variant $JAVA_VARIANT) ==="

docker run --rm \
  -v "$PROJECT_DIR":/src \
  -v "$TOOLS_DIR":/tools:ro \
  -e BUILD_ID="$BUILD_ID" \
  -e STOCK_JAR_NAME="$STOCK_JAR_NAME" \
  "$IMG" bash -c '
  set -e
  SRC=/src/java_patch
  OUT=/src/build/java/classes
  OUTJAR=/src/build/carplay_hook.jar
  CP="/tools/out/$STOCK_JAR_NAME:/tools/libs/org.osgi.framework-1.10.0.jar:/tools/libs/org.osgi.util.tracker-1.5.4.jar"

  rm -rf /src/build/java; mkdir -p "$OUT" /src/build
  SRCLIST=$(mktemp)
  find "$SRC" -name "*.java" -type f > "$SRCLIST"
  echo "Compiling $(wc -l < "$SRCLIST" | tr -d " ") files (target 1.4)..."

  # Generate a CarPlayApp copy with the real BUILD_ID; never edit the source tree.
  GEN=$(mktemp -d)
  mkdir -p "$GEN/com/luka/carplay/core"
  sed "s/@BUILD_ID@/$BUILD_ID/g" "$SRC/com/luka/carplay/core/CarPlayApp.java" > "$GEN/com/luka/carplay/core/CarPlayApp.java"
  grep -v "/com/luka/carplay/core/CarPlayApp.java$" "$SRCLIST" > "$SRCLIST.tmp"; mv "$SRCLIST.tmp" "$SRCLIST"
  printf "%s\n" "$GEN/com/luka/carplay/core/CarPlayApp.java" >> "$SRCLIST"

  # Stock API variant copies replace their originals.
  VAR=/src/build/java_variant
  if [ -d "$VAR" ]; then
    (cd "$VAR" && find . -name "*.java" -type f | sed "s|^\./||") | while read -r rel; do
      grep -v "^$SRC/$rel\$" "$SRCLIST" > "$SRCLIST.tmp"; mv "$SRCLIST.tmp" "$SRCLIST"
      printf "%s\n" "$VAR/$rel" >> "$SRCLIST"
    done
  fi

  javac -source 1.4 -target 1.4 -cp "$CP" -sourcepath "$GEN:/src/build/java_variant:$SRC" -d "$OUT" -Xlint:-options @"$SRCLIST"
  # Compact generated metrics/Unicode tables (VC route text) live inside the jar.
  cp -R /src/java_resources/. "$OUT/"
  (cd "$OUT" && jar cf "$OUTJAR" .)
  rm -rf /src/build/java
'

echo "Output: $PROJECT_DIR/build/carplay_hook.jar"
echo "Build ID: $BUILD_ID"
