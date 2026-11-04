#!/bin/sh
# Build the "Services" edition (HAM / GMRS-FRS / MARINE service modes + FM radio).
#
# Unlike compile-with-docker.sh this does not prune Docker: it only builds the
# image if it is missing and mounts the source tree into it.
#
# The 60 KB flash budget is full: FM radio fits because these are disabled:
# VFO detail view, mic bar, 1750 Hz tone, auto power-off, RX/TX timer
# resume-scan state and the frequency / tone finder. The spectrum analyzer and
# AirCopy stay off.

set -e
IMAGE=uvk5v1
ALPINE_TAG=3.22   # gcc 15.1: the flash budget below was measured with it
cd "$(dirname "$0")"

# (re)build the image when it is missing or was built from another Alpine release
docker run --rm "$IMAGE" grep -q "^VERSION_ID=$ALPINE_TAG" /etc/os-release 2>/dev/null \
    || docker build --build-arg ALPINE_TAG="$ALPINE_TAG" -t "$IMAGE" .

MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd):/app" "$IMAGE" /bin/bash -c "\
    cd /app && make -s clean && find . -name '*.o' -not -path './external/CMSIS*' -delete && make -s \
    ENABLE_FEAT_SERVICE_MODES=1 \
    ENABLE_FEAT_FINDER=0 \
    ENABLE_FMRADIO=1 \
    ENABLE_AUDIO_BAR=0 \
    ENABLE_TX1750=0 \
    ENABLE_VOX=1 \
    ENABLE_FEAT_F4HWN_SLEEP=0 \
    ENABLE_FEAT_F4HWN_RX_TX_TIMER=0 \
    ENABLE_FEAT_F4HWN_RESUME_STATE=0 \
    ENABLE_SMALL_BOLD=0 \
    ENABLE_SCAN_RANGES=0 \
    EDITION_STRING=Services \
    TARGET=f4hwn.services \
    && arm-none-eabi-size f4hwn.services"
