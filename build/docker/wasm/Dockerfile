# KeeperFX build image — WebAssembly (Emscripten)
#
# Bakes in:
#   - Emscripten SDK (latest)
#   - CMake + Ninja
#   - vcpkg (pinned commit) with SDL2/SDL2_image/SDL2_mixer/SDL2_net/glad
#     pre-installed for the wasm32-emscripten triplet
#
# Usage (local):
#   docker run --rm -v $(pwd):/src ghcr.io/<org>/keeperfx-build-wasm:latest \
#     bash -c "cmake --preset wasm-release && cmake --build --preset wasm-release"

FROM emscripten/emsdk:latest

ENV DEBIAN_FRONTEND=noninteractive

# vcpkg commit pinned to match ci-wasm.yml
ARG VCPKG_COMMIT=9558037875497b9db8cf38fcd7db68ec661bffe7

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        curl \
        git \
        ca-certificates \
        pkg-config \
        zip \
        unzip \
        tar \
    && rm -rf /var/lib/apt/lists/*

# ── vcpkg ─────────────────────────────────────────────────────────────────────
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    cd /opt/vcpkg && \
    git checkout ${VCPKG_COMMIT} && \
    ./bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# ── Pre-install vcpkg packages for wasm32-emscripten ─────────────────────────
# Copy vcpkg.json so we can install just the deps listed there.
COPY vcpkg.json /tmp/vcpkg.json
COPY vcpkg-configuration.json /tmp/vcpkg-configuration.json

RUN cd /tmp && \
    /opt/vcpkg/vcpkg install \
        --triplet wasm32-emscripten \
        --x-manifest-root /tmp \
        --x-install-root /opt/vcpkg-installed \
    && rm -f /tmp/vcpkg.json /tmp/vcpkg-configuration.json

ENV VCPKG_INSTALLED_DIR=/opt/vcpkg-installed

WORKDIR /src
