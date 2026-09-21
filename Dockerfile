FROM debian:bookworm-slim

ARG OPENORBIS_ARCHIVE=toolchain-llvm-18.tar.gz
ARG OPENORBIS_SHA256=3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        clang \
        lld \
        make \
    && rm -rf /var/lib/apt/lists/*

COPY .cache/${OPENORBIS_ARCHIVE} /tmp/${OPENORBIS_ARCHIVE}

RUN mkdir -p /opt/openorbis \
    && echo "${OPENORBIS_SHA256}  /tmp/${OPENORBIS_ARCHIVE}" | sha256sum --check - \
    && tar --extract --gzip --file "/tmp/${OPENORBIS_ARCHIVE}" \
        --directory /opt/openorbis --strip-components=1 \
    && rm "/tmp/${OPENORBIS_ARCHIVE}"

ENV OO_PS4_TOOLCHAIN=/opt/openorbis
WORKDIR /workspace

CMD ["make"]
