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
exit $failed
