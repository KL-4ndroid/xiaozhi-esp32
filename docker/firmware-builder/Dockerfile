# syntax=docker/dockerfile:1.7

ARG IDF_IMAGE=espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c
FROM ${IDF_IMAGE}

ARG IDF_IMAGE
ARG FIRMWARE_SOURCE_REVISION=unknown
ARG FIRMWARE_SOURCE_URL=https://github.com/78/xiaozhi-esp32

LABEL org.opencontainers.image.title="XiaoZhi firmware builder" \
      org.opencontainers.image.description="Builds one XiaoZhi board configuration with a selected language and wake word" \
      org.opencontainers.image.source="${FIRMWARE_SOURCE_URL}" \
      org.opencontainers.image.revision="${FIRMWARE_SOURCE_REVISION}"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

WORKDIR /opt/xiaozhi-esp32
COPY . .

RUN sed -i 's/\r$//' docker/firmware-builder/entrypoint.sh \
    && chmod 0755 \
      docker/firmware-builder/entrypoint.sh \
      docker/firmware-builder/firmware_builder.py \
    && . "${IDF_PATH}/export.sh" >/dev/null \
    && idf.py --version \
    && python3 scripts/build.py --list-boards --json >/tmp/xiaozhi-boards.json \
    && python3 scripts/build.py --list-languages --json >/tmp/xiaozhi-languages.json

ENV FIRMWARE_SOURCE_DIR=/opt/xiaozhi-esp32 \
    FIRMWARE_OUTPUT_DIR=/output \
    FIRMWARE_IDF_IMAGE=${IDF_IMAGE} \
    FIRMWARE_SOURCE_REVISION=${FIRMWARE_SOURCE_REVISION} \
    PYTHONUNBUFFERED=1

ENTRYPOINT ["/opt/xiaozhi-esp32/docker/firmware-builder/entrypoint.sh"]
