# KeeperFX build image — Windows x86 cross-compile (i686-w64-mingw32)
# Cross-compiles for Windows x86 from Ubuntu using i686-w64-mingw32 toolchain.
#
# Bakes in:
#   - MinGW-w64 i686 cross-toolchain (posix threads)
#   - CMake + Ninja
#   - SDL2 / SDL2_image / SDL2_mixer / SDL2_net official MinGW prebuilts
#     installed into /usr/i686-w64-mingw32/ (the CMAKE_FIND_ROOT_PATH for
#     mingw32.cmake) so find_package(SDL2 CONFIG REQUIRED) resolves without vcpkg
#   - kfx-deps tarballs in /opt/kfx-deps (read by KFX_DEPS_CACHE in
#     deps/CMakeLists.txt — avoids downloading at every CI run)
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-mingw32:latest \
#     bash -c "cmake --preset windows-x86-release && cmake --build --preset windows-x86-release"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Toolchain & build tools ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-i686-posix \
        g++-mingw-w64-i686-posix \
        cmake \
        ninja-build \
        curl \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ── SDL2 MinGW i686 prebuilts → /usr/i686-w64-mingw32 ───────────────────────
# Versions must match what prebuilds.mk downloads (kept in sync with the repo).
ARG SDL2_VER=2.30.7
ARG SDL2_IMAGE_VER=2.8.2
ARG SDL2_MIXER_VER=2.8.0
ARG SDL2_NET_VER=2.2.0
ARG ARCH=i686-w64-mingw32
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


# deps/CMakeLists.txt reads KFX_DEPS_CACHE before downloading from GitHub.
RUN mkdir -p /opt/kfx-deps && cd /opt/kfx-deps && \
    curl -fsSLo enet-mingw32.tar.gz       "https://github.com/dkfans/kfx-deps/releases/download/initial/enet-mingw32.tar.gz" && \
    curl -fsSLo zlib-mingw32.tar.gz       "https://github.com/dkfans/kfx-deps/releases/download/initial/zlib-mingw32.tar.gz" && \
    curl -fsSLo spng-mingw32.tar.gz       "https://github.com/dkfans/kfx-deps/releases/download/initial/spng-mingw32.tar.gz" && \
    curl -fsSLo astronomy-mingw32.tar.gz  "https://github.com/dkfans/kfx-deps/releases/download/initial/astronomy-mingw32.tar.gz" && \
    curl -fsSLo centijson-mingw32.tar.gz  "https://github.com/dkfans/kfx-deps/releases/download/initial/centijson-mingw32.tar.gz" && \
    curl -fsSLo ffmpeg-mingw32.tar.gz     "https://github.com/dkfans/kfx-deps/releases/download/initial/ffmpeg-mingw32.tar.gz" && \
    curl -fsSLo openal-mingw32.tar.gz     "https://github.com/dkfans/kfx-deps/releases/download/2024-11-14/openal-mingw32.tar.gz" && \
    curl -fsSLo luajit-mingw32.tar.gz     "https://github.com/dkfans/kfx-deps/releases/download/20250418/luajit-mingw32.tar.gz" && \
    curl -fsSLo miniupnpc-mingw32.tar.gz  "https://github.com/dkfans/kfx-deps/releases/download/20260102/miniupnpc-mingw32.tar.gz" && \
    curl -fsSLo libnatpmp-mingw32.tar.gz  "https://github.com/dkfans/kfx-deps/releases/download/20260102/libnatpmp-mingw32.tar.gz"

ENV KFX_DEPS_CACHE=/opt/kfx-deps

WORKDIR /src
