#!/bin/bash
# Turns the packed Linux build (_ci/pkg/HEXAHEDRON) into a .deb for Ubuntu/Debian.
#   usage: make_deb.sh <version>     output: dist/hexahedron_<version>_amd64.deb
set -euo pipefail
VER=${1#v}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=$ROOT/_ci/deb
rm -rf "$D"
mkdir -p "$D/DEBIAN" "$D/opt" "$D/usr/games" "$D/usr/share/applications" \
         "$D/usr/share/icons/hicolor/192x192/apps"
cp -r "$ROOT/_ci/pkg/HEXAHEDRON" "$D/opt/hexahedron"
ln -s /opt/hexahedron/hexahedron "$D/usr/games/hexahedron"
cp "$ROOT/src/platform/android/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" \
   "$D/usr/share/icons/hicolor/192x192/apps/hexahedron.png"
cat > "$D/usr/share/applications/hexahedron.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=HEXAHEDRON
Comment=Dice puzzle game inspired by Hexaxis XXI
Exec=/usr/games/hexahedron
Icon=hexahedron
Categories=Game;LogicGame;
DESK
cat > "$D/DEBIAN/control" <<CTRL
Package: hexahedron
Version: $VER
Architecture: amd64
Maintainer: Xma1 <17881616+markxma1@users.noreply.github.com>
Depends: libc6 (>= 2.35), libgl1, libx11-6
Section: games
Priority: optional
Description: Dice puzzle game inspired by Hexaxis XXI
 3D dice puzzle game with procedural music and sound.
CTRL
mkdir -p "$ROOT/dist"
dpkg-deb --root-owner-group --build "$D" "$ROOT/dist/hexahedron_${VER}_amd64.deb"
