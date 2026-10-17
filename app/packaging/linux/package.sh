#!/usr/bin/env bash
# AppImage and .deb from a release build. Both carry Qt; the .deb installs it under /opt/vial-armed.
#
# Usage: package.sh <build dir> <output dir> <version>
# Needs qmake6 (or qmake) of the Qt the app was built with in PATH; downloads linuxdeploy into $TOOLS.
set -euo pipefail

BUILD=$(realpath "$1")
OUT=$(mkdir -p "$2" && realpath "$2")
VERSION=$3
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=${TOOLS:-$OUT/.tools}

fetch() { # url sha256 file
    [ -x "$TOOLS/$3" ] && return
    mkdir -p "$TOOLS"
    curl -sSL -o "$TOOLS/$3" "$1"
    echo "$2  $TOOLS/$3" | sha256sum -c --quiet
    chmod +x "$TOOLS/$3"
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d linuxdeploy-x86_64.AppImage
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724 linuxdeploy-plugin-qt-x86_64.AppImage

# ---- AppImage

APPDIR=$OUT/AppDir
rm -rf "$APPDIR"
DESTDIR=$APPDIR cmake --install "$BUILD" --prefix /usr
# only the xcb platform plugin is deployed (XWayland on Wayland desktops), as in Vial Armed 1.x
mkdir -p "$APPDIR/apprun-hooks"
printf 'export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"\n' > "$APPDIR/apprun-hooks/vial-armed-xcb.sh"
export PATH=$TOOLS:$PATH
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE in containers
# the strip bundled with linuxdeploy predates newer toolchains' sections
export NO_STRIP=1
export LDAI_OUTPUT=$OUT/vial-armed-$VERSION-x86_64.AppImage
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/vial-armed.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/vial-armed.png" \
    --plugin qt
# plugins the app never uses pull in Qt Quick, Qt PDF and image codecs: drop them, then every library
# nothing left needs
rm -f "$APPDIR"/usr/plugins/platforminputcontexts/libqtvirtualkeyboardplugin.so \
      "$APPDIR"/usr/plugins/imageformats/lib{qpdf,qtiff,qwebp,qtga,qwbmp,qicns}.so
needed() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
keep=$(mktemp)
{ needed "$APPDIR/usr/bin/vial-armed"; find "$APPDIR/usr/plugins" -name '*.so' | while read -r p; do needed "$p"; done; } | sort -u > "$keep"
while :; do
    before=$(wc -l < "$keep")
    while read -r l; do [ -f "$APPDIR/usr/lib/$l" ] && needed "$APPDIR/usr/lib/$l"; done < "$keep" | cat - "$keep" | sort -u > "$keep.new"
    mv "$keep.new" "$keep"
    [ "$(wc -l < "$keep")" = "$before" ] && break
done
for f in "$APPDIR"/usr/lib/*.so*; do
    grep -qxF "$(basename "$f")" "$keep" || rm -f "$f"
done
rm -f "$keep"
# AppRun starts the binary through a symlink in the AppDir root, and Qt looks for qt.conf next to that
printf '[Paths]\nPrefix = usr\nPlugins = plugins\n' > "$APPDIR/qt.conf"
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" --output appimage

# ---- .deb

PKG=$OUT/deb
rm -rf "$PKG"
mkdir -p "$PKG/opt/vial-armed" "$PKG/usr/bin" "$PKG/usr/share" "$PKG/usr/lib/udev/rules.d" "$PKG/DEBIAN"
cp -a "$APPDIR/usr" "$PKG/opt/vial-armed/"
mv "$PKG/opt/vial-armed/usr/share/applications" "$PKG/opt/vial-armed/usr/share/icons" "$PKG/usr/share/"
mv "$PKG/opt/vial-armed/usr/lib/udev/rules.d/"* "$PKG/usr/lib/udev/rules.d/"
rm -rf "$PKG/opt/vial-armed/usr/lib/udev"
# not a symlink: Qt looks for qt.conf next to the path it was started by
printf '#!/bin/sh\nexport QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"\nexec /opt/vial-armed/usr/bin/vial-armed "$@"\n' > "$PKG/usr/bin/vial-armed"
chmod 755 "$PKG/usr/bin/vial-armed"
SIZE=$(du -sk --exclude=DEBIAN "$PKG" | cut -f1)
cat > "$PKG/DEBIAN/control" <<EOF
Package: vial-armed
Version: $VERSION
Architecture: amd64
Maintainer: backflipinc <riddlah@yandex.ru>
Installed-Size: $SIZE
Depends: libc6 (>= 2.35), libudev1, libgl1, libegl1, libfontconfig1, libfreetype6, libx11-6, libx11-xcb1, libxkbcommon0, libxkbcommon-x11-0
Section: utils
Priority: optional
Homepage: https://github.com/backflipinc/vial-gui-armed
Description: Keyboard configurator for Vial and qmk-armed keyboards
 Vial Armed edits the keymap, macros, lighting, themes and screen of Vial
 keyboards, with extra pages for keyboards running qmk-armed.
EOF
cat > "$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=hidraw || true
fi
EOF
chmod 755 "$PKG/DEBIAN/postinst"
DEB=$OUT/vial-armed_${VERSION}_amd64.deb
if command -v dpkg-deb >/dev/null; then
    dpkg-deb --root-owner-group -Zxz --build "$PKG" "$DEB"
else
    # what dpkg-deb writes: an ar archive of debian-binary, control.tar.xz and data.tar.xz
    T=$(mktemp -d)
    echo "2.0" > "$T/debian-binary"
    tar --owner=0 --group=0 -C "$PKG/DEBIAN" -cJf "$T/control.tar.xz" .
    tar --owner=0 --group=0 -C "$PKG" --exclude=./DEBIAN -cJf "$T/data.tar.xz" .
    rm -f "$DEB"
    (cd "$T" && ar rc "$DEB" debian-binary control.tar.xz data.tar.xz)
    rm -rf "$T"
fi
ls -l "$LDAI_OUTPUT" "$DEB"
