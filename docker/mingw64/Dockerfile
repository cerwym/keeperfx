# KeeperFX build image — Windows x64 cross-compile (x86_64-w64-mingw32)
#
# Bakes in:
#   - MinGW-w64 x86_64 cross-toolchain (posix threads)
#   - CMake + Ninja
#   - SDL2 / SDL2_image / SDL2_mixer / SDL2_net official MinGW x86_64 prebuilts
#     installed into /usr/x86_64-w64-mingw32/ (the CMAKE_FIND_ROOT_PATH for
#     mingw64.cmake) so find_package(SDL2 CONFIG REQUIRED) resolves without vcpkg
#
# NOTE: kfx-deps (enet, ffmpeg, openal, luajit, etc.) currently only have
# mingw32 (i686) prebuilts in dkfans/kfx-deps.  Once x86_64 prebuilts are
# published the windows-x64-release build will be fully functional.
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-mingw64:latest \
#     bash -c "cmake --preset windows-x64-release && cmake --build --preset windows-x64-release"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Toolchain & build tools ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
        cmake \
        ninja-build \
        curl \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ── SDL2 MinGW x86_64 prebuilts → /usr/x86_64-w64-mingw32 ───────────────────
ARG SDL2_VER=2.30.7
ARG SDL2_IMAGE_VER=2.8.2
ARG SDL2_MIXER_VER=2.8.0
ARG SDL2_NET_VER=2.2.0
ARG ARCH=x86_64-w64-mingw32
ARG PREFIX=/usr/${ARCH}

RUN set -eux; \
    TMP=$(mktemp -d); \
    # SDL2
    curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VER}/SDL2-devel-${SDL2_VER}-mingw.tar.gz" \
        | tar -xz -C "${TMP}"; \
    cp -r "${TMP}/SDL2-${SDL2_VER}/${ARCH}/include/." "${PREFIX}/include/"; \
    cp -r "${TMP}/SDL2-${SDL2_VER}/${ARCH}/lib/."     "${PREFIX}/lib/"; \
    rm -rf "${TMP}"; \
    # SDL2_image
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VER}/SDL2_image-devel-${SDL2_IMAGE_VER}-mingw.tar.gz" \
        | tar -xz -C "${TMP}"; \
    cp -r "${TMP}/SDL2_image-${SDL2_IMAGE_VER}/${ARCH}/include/." "${PREFIX}/include/"; \
    cp -r "${TMP}/SDL2_image-${SDL2_IMAGE_VER}/${ARCH}/lib/."     "${PREFIX}/lib/"; \
    rm -rf "${TMP}"; \
    # SDL2_mixer
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL2_MIXER_VER}/SDL2_mixer-devel-${SDL2_MIXER_VER}-mingw.tar.gz" \
        | tar -xz -C "${TMP}"; \
    cp -r "${TMP}/SDL2_mixer-${SDL2_MIXER_VER}/${ARCH}/include/." "${PREFIX}/include/"; \
    cp -r "${TMP}/SDL2_mixer-${SDL2_MIXER_VER}/${ARCH}/lib/."     "${PREFIX}/lib/"; \
    rm -rf "${TMP}"; \
    # SDL2_net
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/libsdl-org/SDL_net/releases/download/release-${SDL2_NET_VER}/SDL2_net-devel-${SDL2_NET_VER}-mingw.tar.gz" \
        | tar -xz -C "${TMP}"; \
    cp -r "${TMP}/SDL2_net-${SDL2_NET_VER}/${ARCH}/include/." "${PREFIX}/include/"; \
    cp -r "${TMP}/SDL2_net-${SDL2_NET_VER}/${ARCH}/lib/."     "${PREFIX}/lib/"; \
    rm -rf "${TMP}"

# ── curl MinGW x86_64 prebuilts ─────────────────────────────────────────────
ARG CURL_VER=8.12.1_2
RUN set -eux; \
    TMP=$(mktemp -d); \
    curl -fsSL "https://curl.se/windows/dl-${CURL_VER}/curl-${CURL_VER}-win64-mingw.zip" -o "${TMP}/curl.zip"; \
    unzip -q "${TMP}/curl.zip" -d "${TMP}/curl"; \
    CURL_DIR="${TMP}/curl/curl-${CURL_VER}-win64-mingw"; \
    cp -r "${CURL_DIR}/include/." "${PREFIX}/include/"; \
    cp    "${CURL_DIR}/lib/libcurl.dll.a" "${PREFIX}/lib/" 2>/dev/null || true; \
    cp    "${CURL_DIR}/lib/libcurl.a"     "${PREFIX}/lib/" 2>/dev/null || true; \
    rm -rf "${TMP}"

WORKDIR /src
