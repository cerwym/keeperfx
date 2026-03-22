# KeeperFX Switch SDK build image
# Base image (devkitpro/devkita64) includes the full libnx toolchain with aarch64-none-elf.
# See: https://devkitpro.org/wiki/devkitPro_pacman
#
# Add debugging and development tools on top of base SDK.
FROM devkitpro/devkita64:latest

# gdb-multiarch: drop-in replacement for aarch64-based debugging.
# python3-pip + pyelftools: for ELF parsing and crash analysis tools.
# git, cmake, ninja-build: common development and build tools.
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends gdb-multiarch python3-pip git cmake ninja-build \
    && pip3 install --no-cache-dir --break-system-packages pyelftools \
    && rm -rf /var/lib/apt/lists/*
