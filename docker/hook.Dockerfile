# Build image for the universal preload (Toolbox/carplay_alt_screen/src/build_qnx_arm.sh):
# a freestanding clang/lld ARMv7 build with its own libc/libm stubs, checked with
# arm-linux-gnueabihf-readelf. No QNX SDP is needed for this one library.
# trixie: Debian clang/LLD 19.1.7, the compiler of the V3.8 Fix1 release library.
#
#   docker build --platform=linux/amd64 -t mmi-cockpit-hook-builder -f docker/hook.Dockerfile docker
FROM debian:trixie-slim
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang lld binutils-arm-linux-gnueabihf python3 python3-pyelftools file && \
    rm -rf /var/lib/apt/lists/*
