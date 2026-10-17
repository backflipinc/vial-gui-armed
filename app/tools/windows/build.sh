#!/usr/bin/env bash
# In the vial-armed-win container: configure, build and run the tests like CI's Windows job.
set -euo pipefail
SRC=${SRC:-/src}
BUILD=${BUILD:-/src/app/build-win}
export PATH=/opt/msvc/bin/x64:$PATH
cmake -S "$SRC/app" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
    -DCMAKE_PREFIX_PATH=/opt/qt/6.8.3/msvc2022_64 -DQT_HOST_PATH=/opt/qt/6.8.3/gcc_64
cmake --build "$BUILD"
# the tests find Qt's DLLs through Wine's PATH
export WINEPATH="Z:\\opt\\qt\\6.8.3\\msvc2022_64\\bin"
export QT_QPA_PLATFORM=offscreen
failed=0
for t in $(find "$BUILD" -maxdepth 2 -name 'tst_*.exe' | sort); do
    log=${t%.exe}.log
    # Wine drops a console program's output: QTest writes it to a file
    if wine "$t" -o "$(winepath -w "$log" 2>/dev/null)",txt > /dev/null 2>&1; then echo "PASS $(basename "$t")"; else echo "FAIL $(basename "$t")"; tail -30 "$log"; failed=1; fi
done
[ $failed = 0 ] || exit 1

# the package step of CI's Windows job: windeployqt, the zip and the installer
DEPLOY=$BUILD/deploy
rm -rf "$DEPLOY" && mkdir -p "$DEPLOY"
cp "$(find "$BUILD" -name vial-armed.exe | head -1)" "$DEPLOY/"
wine /opt/qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release --no-translations --no-system-d3d-compiler \
    --no-opengl-sw "$(winepath -w "$DEPLOY/vial-armed.exe")" > "$BUILD/windeployqt.log" 2>&1
test -f "$DEPLOY/platforms/qwindows.dll"
command -v makensis > /dev/null || { apt-get update -qq && apt-get install -y -qq nsis > /dev/null; }
(cd "$SRC/app/packaging/windows" && makensis -V2 -DVERSION=0.0.0 -DSRC="$DEPLOY" -DOUT="$BUILD/vial-armed-setup.exe" installer.nsi)
ls -l "$BUILD/vial-armed-setup.exe"
