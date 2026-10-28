#!/bin/bash
# Builds a self-contained PC release (static SDL3) and packs it.
#   usage: build_pc.sh <package-name> [extra cmake args for SDL3 and the game]
# Output: dist/<package-name>.(tar.gz|zip)
set -euo pipefail
NAME=$1; shift
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$ROOT/_ci
SDL_VER=3.4.18
mkdir -p "$WORK" "$ROOT/dist"
cd "$WORK"

if [ ! -d "SDL3-$SDL_VER" ]; then
  curl -sSL -o sdl.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz"
  tar -xzf sdl.tar.gz
fi
cmake -S "SDL3-$SDL_VER" -B sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="$WORK/sdl" "$@"
cmake --build sdl-build
cmake --install sdl-build

cmake -S "$ROOT" -B game -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$WORK/sdl" "$@"
cmake --build game
./game/core_tests
./game/touch_tests

PKG=$WORK/pkg/HEXAHEDRON
rm -rf "$WORK/pkg"; mkdir -p "$PKG"
EXE=game/hexahedron; [ -f game/hexahedron.exe ] && EXE=game/hexahedron.exe
cp "$EXE" "$PKG/"
cp -r "$ROOT/assets" "$PKG/assets"
cp "$ROOT/LICENSE" "$ROOT/README.md" "$PKG/"
cd "$WORK/pkg"
case "$NAME" in
  *linux*) tar -czf "$ROOT/dist/$NAME.tar.gz" HEXAHEDRON ;;
  *) if command -v zip >/dev/null; then zip -qr "$ROOT/dist/$NAME.zip" HEXAHEDRON
     else 7z a -bd "$ROOT/dist/$NAME.zip" HEXAHEDRON >/dev/null; fi ;;
esac
ls -l "$ROOT/dist"
