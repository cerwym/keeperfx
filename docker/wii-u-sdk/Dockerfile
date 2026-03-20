# KeeperFX Wii U SDK build image
# Base image (devkitpro/devkitppc) includes the full Wii U SDK toolchain
# and PowerPC cross-compiler (powerpc-eabi-*).
# See: https://hub.docker.com/r/devkitpro/devkitppc
#
# Add debugging tools and development utilities on top.
FROM devkitpro/devkitppc:latest

# Add debugging and development tools:
# gdb-multiarch: drop-in replacement for powerpc-eabi-gdb (more stable)
# python3-pip + pyelftools: for ELF parsing and crash analysis
# git, cmake, ninja-build: common build dependencies
RUN apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
       gdb-multiarch python3-pip git cmake ninja-build \
    && pip3 install --no-cache-dir --break-system-packages pyelftools \
    && rm -rf /var/lib/apt/lists/*
