#!/bin/bash

set -e

export LD_LIBRARY_PATH=/vial-gui/util/python36/prefix/lib/

cd /vial-gui
./util/python36/prefix/bin/python3 -m venv docker_venv
. docker_venv/bin/activate
pip install Cython==0.29.25
pip install -r requirements.txt
fbs freeze
fbs installer
deactivate
/pkg2appimage-*/pkg2appimage misc/vial-armed.yml
mv out/vial-armed-*.AppImage /output/vial-armed-x86_64.AppImage
cp target/vial-armed.deb /output/vial-armed_amd64.deb
