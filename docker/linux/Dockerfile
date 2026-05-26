# KeeperFX build image — Linux x86_64 native build
#
# Bakes in all SDL2 and system library dev packages so the linux-x64-release
# CMake preset works without any network access during the build.
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-linux:latest \
#     bash -c "cmake --preset linux-x64-release && cmake --build --preset linux-x64-release"

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
        git \
        ca-certificates \
        # SDL2
        libsdl2-dev \
        libsdl2-image-dev \
        libsdl2-mixer-dev \
        libsdl2-net-dev \
        # Audio
        libopenal-dev \
        # Video
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libswresample-dev \
        # Scripting
        libluajit-5.1-dev \
        # Networking
        libminiupnpc-dev \
        libnatpmp-dev \
        libcurl4-openssl-dev \
        # Image/compression
        libspng-dev \
        libminizip-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
