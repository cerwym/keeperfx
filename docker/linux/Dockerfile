# KeeperFX build image — Linux x86_64 native build
#
# Bakes in all system library dev packages that vcpkg needs as build-time
# dependencies (X11, ALSA, Wayland, OpenGL headers, etc.) plus the core
# build toolchain and vcpkg prerequisites (zip, python3, nasm).
#
# All KeeperFX library dependencies (SDL2, FFmpeg, OpenAL, LuaJIT, enet6, etc.)
# are provided by vcpkg via the x64-linux-keeperfx triplet.  The vcpkg binary
# cache (GitHub Actions x-gha) keeps subsequent CI runs fast.
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-linux:latest \
#     bash -c "external/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
#              && cmake --preset linux-x64-release \
#              && cmake --build --preset linux-x64-release"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        g++ \
        cmake \
        ninja-build \
    clang-format \
    clang-tidy \
    cppcheck \
        pkg-config \
        curl \
        zip \
        unzip \
        git \
        ca-certificates \
        python3 \
        nasm \
        # SDL2 system dependencies (needed for vcpkg's SDL2 port to compile)
        libx11-dev \
        libxext-dev \
        libxrandr-dev \
        libxcursor-dev \
        libxi-dev \
        libxinerama-dev \
        libxfixes-dev \
        libxrender-dev \
        libxkbcommon-dev \
        libwayland-dev \
        libwayland-bin \
        wayland-protocols \
        libasound2-dev \
        libpulse-dev \
        libgl1-mesa-dev \
        libgles2-mesa-dev \
        libdrm-dev \
        libgbm-dev \
        libdbus-1-dev \
        # FFmpeg system dependencies
        libvorbis-dev \
        libopus-dev \
        libmp3lame-dev \
        # OpenAL system dependencies
        libsndfile1-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
