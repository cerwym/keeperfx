# KeeperFX 3DS build image
# Base image (devkitpro/devkitarm) includes the full 3DS SDK toolchain.
# See: https://hub.docker.com/r/devkitpro/devkitarm
#
# Add debugging and development tools needed for KeeperFX 3DS cross-compilation.
FROM devkitpro/devkitarm:latest

RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
        gdb-multiarch \
        python3-pip \
        git \
        cmake \
        ninja-build \
    && pip3 install --no-cache-dir --break-system-packages pyelftools \
    && rm -rf /var/lib/apt/lists/*
