# KeeperFX vitasdk build image
# Extends gnuton/vitasdk-docker with liblame cross-compiled for arm-vita-eabi.
# liblame is required by vitaGL's HAVE_PROFILING=1 build (screen recorder).
FROM gnuton/vitasdk-docker:latest

ENV VITASDK=/usr/local/vitasdk
ENV PATH=$VITASDK/bin:$PATH
ENV LAME_VERSION=3.100

RUN apt-get update && apt-get install -y --no-install-recommends \
        wget \
    && rm -rf /var/lib/apt/lists/* \
    && wget -q "https://downloads.sourceforge.net/lame/lame-${LAME_VERSION}.tar.gz" \
    && tar xf "lame-${LAME_VERSION}.tar.gz" \
    && cd "lame-${LAME_VERSION}" \
    && ./configure \
        --host=arm-vita-eabi \
        --prefix="${VITASDK}/arm-vita-eabi" \
        --disable-shared \
        --enable-static \
        --disable-frontend \
        --disable-decoder \
        --disable-gtktest \
        --disable-cpml \
        --disable-asm \
        CC=arm-vita-eabi-gcc \
        AR=arm-vita-eabi-ar \
        RANLIB=arm-vita-eabi-ranlib \
        NM=arm-vita-eabi-nm \
        CFLAGS="-O2 -mcpu=cortex-a9 -mfpu=neon" \
    && make -j"$(nproc)" \
    && make install \
    && cd .. \
    && rm -rf "lame-${LAME_VERSION}" "lame-${LAME_VERSION}.tar.gz"

# vitashark (GLSL→CG compiler) and mathneon (NEON math) are required by vitaGL
# when built with HAVE_GLSL_SUPPORT=1.  CI installs these via vdpm; mirror that here
# so local Docker builds are consistent with CI.
RUN $VITASDK/bin/vdpm vitashark mathneon
