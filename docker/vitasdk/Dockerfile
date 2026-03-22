# KeeperFX vitasdk build image
# Base image (ghcr.io/cerwym/build-vitasdk) includes the full Vita SDK toolchain
# plus liblame, vitashark, mathneon, and ninja-build.
# See: https://github.com/cerwym/build-vitasdk
#
# Add any keeperfx-specific extras below if needed.
FROM ghcr.io/cerwym/build-vitasdk:latest

# arm-vita-eabi-gdb ships in the SDK but segfaults (libpython3.10 issue).
# gdb-multiarch works as a drop-in replacement for remote debugging.
# python3-pip + pyelftools: needed by tools/vita-parse-crash.sh (crash dump parser).
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends gdb-multiarch python3-pip netcat-openbsd \
    && pip3 install --no-cache-dir pyelftools \
    && rm -rf /var/lib/apt/lists/*
