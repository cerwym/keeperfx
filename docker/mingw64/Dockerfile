# KeeperFX build image — Windows x64 cross-compile (x86_64-w64-mingw32)
#
# Bakes in:
#   - MinGW-w64 x86_64 cross-toolchain (posix threads)
#   - CMake + Ninja + git (required by vcpkg bootstrap)
#   - zip / unzip / python3 / nasm (required by vcpkg port builds)
#
# All external libraries (SDL2, FFmpeg, OpenAL, LuaJIT, enet6, etc.) are
# provided by vcpkg via the x64-mingw-cross triplet, which chainloads
# build/cmake/toolchains/mingw64-posix.cmake.  The vcpkg binary cache
# (GitHub Actions x-gha) keeps subsequent CI runs fast.
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-mingw64:latest \
#     bash -c "external/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
#              && cmake --preset windows-x64-release \
#              && cmake --build --preset windows-x64-release"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Toolchain & build tools ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
        cmake \
        ninja-build \
        curl \
        zip \
        unzip \
        git \
        ca-certificates \
        python3 \
        nasm \
        autoconf \
        automake \
        libtool \
        perl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
