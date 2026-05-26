# KeeperFX build image — Windows x86 cross-compile (i686-w64-mingw32)
# Cross-compiles for Windows x86 from Ubuntu using i686-w64-mingw32 toolchain.
#
# Bakes in:
#   - MinGW-w64 i686 cross-toolchain (win32 threads)
#   - CMake + Ninja + git (required by vcpkg bootstrap)
#   - zip / unzip / python3 / nasm (required by vcpkg port builds)
#   - KFX build tools (png2ico, pngpal2raw, etc.) for asset generation
#
# All external libraries (SDL2, FFmpeg, OpenAL, LuaJIT, enet6, etc.) are
# provided by vcpkg via the x86-mingw-cross triplet, which chainloads
# build/cmake/toolchains/mingw32-posix.cmake.  The vcpkg binary cache
# (GitHub Actions x-gha) keeps subsequent CI runs fast.
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-mingw32:latest \
#     bash -c "external/vcpkg/bootstrap-vcpkg.sh -disableMetrics \
#              && cmake --preset windows-x86-release \
#              && cmake --build --preset windows-x86-release"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Toolchain & build tools ──────────────────────────────────────────────────
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-i686-win32 \
        g++-mingw-w64-i686-win32 \
        cmake \
        ninja-build \
        gdb \
        gdb-mingw-w64 \
        gdb-mingw-w64-target \
        wine \
        wine32 \
        curl \
        zip \
        unzip \
        git \
        ca-certificates \
        python3 \
        nasm \
    && rm -rf /var/lib/apt/lists/*

# ── KFX build tools → /usr/local/bin ────────────────────────────────────────
# These Linux-native tools are required at CMake configure/build time to
# generate .ico, palette, .ngdat, sound bank, and RNC files.
# Baking them in avoids a per-configure download and eliminates Windows NTFS
# symlink issues that occur when cmake -E tar extracts to a bind-mounted volume.
ARG PNG2ICO_VER=2024-10-28
ARG PNGPAL2RAW_VER=1.0.2
ARG PNG2BESTPAL_VER=1.0.3
ARG PO2NGDAT_VER=1.0.2.31-c
ARG SNDBANKER_VER=1.0.1
ARG RNCTOOLS_VER=1.0.2

RUN set -eux; \
    TMP=$(mktemp -d); \
    # png2ico
    curl -fsSL "https://github.com/dkfans/png2ico/releases/download/${PNG2ICO_VER}/png2ico-lin-${PNG2ICO_VER}.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f -name "png2ico" -exec install -m755 {} /usr/local/bin/png2ico \;; \
    rm -rf "${TMP}"; \
    # pngpal2raw
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/dkfans/pngpal2raw/releases/download/v${PNGPAL2RAW_VER}/pngpal2raw-1_0_2_35-devel-lin.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f -name "pngpal2raw" -exec install -m755 {} /usr/local/bin/pngpal2raw \;; \
    rm -rf "${TMP}"; \
    # png2bestpal
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/dkfans/png2bestpal/releases/download/v${PNG2BESTPAL_VER}/png2bestpal-1_0_3_21-devel-lin.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f -name "png2bestpal" -exec install -m755 {} /usr/local/bin/png2bestpal \;; \
    rm -rf "${TMP}"; \
    # po2ngdat
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/dkfans/po2ngdat/releases/download/v${PO2NGDAT_VER}/po2ngdat-1_0_2_31-devel-lin.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f -name "po2ngdat" -exec install -m755 {} /usr/local/bin/po2ngdat \;; \
    rm -rf "${TMP}"; \
    # sndbanker
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/dkfans/sndbanker/releases/download/v${SNDBANKER_VER}/sndbanker-1_0_1_13-devel-lin.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f -name "sndbanker" -exec install -m755 {} /usr/local/bin/sndbanker \;; \
    rm -rf "${TMP}"; \
    # rnctools (provides rnc + dernc)
    TMP=$(mktemp -d); \
    curl -fsSL "https://github.com/dkfans/rnctools/releases/download/v${RNCTOOLS_VER}/rnctools-1_0_2_5-devel-lin.tar.gz" \
        | tar -xz -C "${TMP}"; \
    find "${TMP}" -type f \( -name "rnc" -o -name "dernc" \) -exec install -m755 {} /usr/local/bin/ \;; \
    rm -rf "${TMP}"


WORKDIR /src
