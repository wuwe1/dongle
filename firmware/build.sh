#!/usr/bin/env bash
# Builds the firmware in ESP-IDF's own docker image: nothing to install but docker.
#   ./build.sh [target...]       targets: esp32s3 (default), esp32c3
# Output: build-<target>/dongle.bin, plus the flash command idf.py prints. Each target keeps its own
# sdkconfig under build-<target>/, so targets do not overwrite each other.
set -euo pipefail
cd "$(dirname "$0")"
IDF_IMAGE="${IDF_IMAGE:-espressif/idf:v5.3.2}"
for t in "${@:-esp32s3}"; do
  echo "== $t =="
  docker run --rm -v "$PWD:/project" -w /project -e HOME=/tmp -u "$(id -u):$(id -g)" "$IDF_IMAGE" \
    idf.py -B "build-$t" -D "SDKCONFIG=build-$t/sdkconfig" -D "IDF_TARGET=$t" build
done
