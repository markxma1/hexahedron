#!/bin/bash
# Builds a cgcomp (PSL1GHT shader compiler) that does NOT pass -fastprecision to Cg.
# With -fastprecision, Cg mixes half (H) and full (R) registers; on the RSX they share
# storage and the lighting shader came out wrong. Full precision costs nothing here.
#   usage: tools/build_cgcomp.sh <PSL1GHT source checkout>
set -e
SRC=${1:?path to a PSL1GHT source checkout (github.com/ps3dev/PSL1GHT)}/tools/cgcomp
OUT=$(cd "$(dirname "$0")" && pwd)/cgcomp-full
TMP=$(mktemp -d)
cp -r "$SRC/source" "$SRC/include" "$TMP/"
sed -i 's/"-fastprecision", //' "$TMP/source/main.cpp"
grep -q fastprecision "$TMP/source/main.cpp" && { echo "patch failed"; exit 1; }
g++ -O2 -w -I"$TMP/include" "$TMP"/source/*.cpp -o "$OUT" -ldl
rm -rf "$TMP"
echo "built $OUT"
