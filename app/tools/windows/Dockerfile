# Windows build and tests on Linux: MSVC (msvc-wine) and Qt for MSVC, the tests run under Wine.
#   docker build -t vial-armed-win app/tools/windows
#   docker run --rm -v "$PWD":/src vial-armed-win /src/app/tools/windows/build.sh
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
        wine64 wine winbind python3 python3-pip python3-venv msitools ca-certificates git cmake ninja-build \
        xvfb curl && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/mstorsjo/msvc-wine /opt/msvc-wine \
    && cd /opt/msvc-wine && ./vsdownload.py --accept-license --dest /opt/msvc \
    && ./install.sh /opt/msvc && rm -rf /root/.cache
COPY aqt.ini /opt/aqt.ini
# mirrors drop a download now and then (the sha1 check catches it): a few tries
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install aqtinstall==3.3.0 \
    && for i in 1 2 3 4; do /opt/aqt/bin/aqt -c /opt/aqt.ini install-qt windows desktop 6.8.3 win64_msvc2022_64 -O /opt/qt && break; done \
    && for i in 1 2 3 4; do /opt/aqt/bin/aqt -c /opt/aqt.ini install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/qt && break; done \
    && test -f /opt/qt/6.8.3/msvc2022_64/bin/Qt6Core.dll && test -x /opt/qt/6.8.3/gcc_64/libexec/moc
ENV WINEDEBUG=-all WINEPREFIX=/opt/wineprefix
RUN wineboot --init 2>/dev/null; wineserver -w; true
