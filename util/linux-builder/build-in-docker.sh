#!/bin/bash

set -e

HERE=$(cd "$(dirname "$0")" && pwd)

cd $HERE
rm -rf output && mkdir -p output

cd $HERE/../..

docker build --network host -t vialguibuilder:latest -f util/linux-builder/Dockerfile .
docker run --privileged --network host --rm -v $(realpath util/linux-builder/output):/output vialguibuilder:latest bash /vial-gui/util/linux-builder/_builder.sh

cd $HERE
ls -lah output
