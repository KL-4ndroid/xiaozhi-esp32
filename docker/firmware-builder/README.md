# Firmware builder container

This image builds one board configuration. The caller selects the board
directory, board name, UI language, and ESP-SR wake-word model. The builder
derives the OTA-reported board type from the selected board's `config.json`.

## Build the image

```bash
docker build \
  --platform linux/arm64 \
  --build-arg FIRMWARE_SOURCE_REVISION="$(git rev-parse HEAD)" \
  -f docker/firmware-builder/Dockerfile \
  -t xiaozhi/firmware-builder:idf61-arm64 .
```

The base image is pinned to the published ESP-IDF v6.1 image digest. The
repository's `dependencies.lock` pins every managed component version and
content hash used by the ESP32-C3 build. Update either pin deliberately and
verify a clean full build before accepting the change.

`scripts/build.py` configures the target, generated sdkconfig defaults, and
board name in one `idf.py reconfigure` call. Component Manager resolves and
populates `managed_components` during that step, so each fresh ECI source clone
must have outbound network access.

## Run one build

```bash
docker run --rm --platform linux/arm64 \
  -e FIRMWARE_BOARD_DIR=xmini/c3 \
  -e FIRMWARE_BOARD_NAME=xmini-c3 \
  -e FIRMWARE_LANGUAGE=zh-CN \
  -e FIRMWARE_WAKE_WORD=nihaoxiaozhi \
  -v "$PWD/output:/output" \
  xiaozhi/firmware-builder:idf61-arm64
```

The board fields intentionally follow `main/boards/**/config.json`:

- `board_dir` is the path relative to `main/boards` and is passed as the
  positional argument to `scripts/build.py`;
- `board_type` is derived from the top-level `type` reported by the firmware
  to OTA; callers do not supply it;
- `board_name` is the selected `builds[].name` and is passed to
  `scripts/build.py --name`.

The builder validates `board_dir` and `board_name` against the checked-out
source and records the derived `board_type` before starting a build.

Each successful job writes:

- `xiaozhi.bin`: application/OTA image;
- `generated_assets.bin`: assets partition image;
- `merged-binary.bin`: full flash image;
- `build.log`: complete compiler output;
- `manifest.json`: inputs, tool versions, source revision, sizes, and SHA-256
  checksums, including the IDF image reference and component lock checksum.
  Its `partition_flash` section contains the ESP-IDF-derived app/assets offsets
  and an esptool argument list that deliberately omits NVS, the bootloader,
  partition table, OTA data, and PHY initialization regions.

## Reproducible ESP-Hi build on Windows

From PowerShell, run the repository wrapper. It refuses to reuse a non-empty
output directory, builds the pinned container image, and then builds the
ESP32-C3 `esp-hi` firmware with the locked components:

```powershell
.\scripts\build-esp-hi-repro.ps1 -OutputDirectory ..\repro-build-esp-hi
```

The fixed defaults are `zh-CN` and `nihaoxiaozhi` (resolved to the C3-compatible
`wn9s_nihaoxiaozhi` model). To produce another explicitly recorded locale, pass
`-Language zh-TW`; the manifest records every selected input.

To upload the job output to an HTTP artifact receiver, also pass:

```text
FIRMWARE_UPLOAD_URL=https://example.com/api/firmware-builds
FIRMWARE_UPLOAD_TOKEN=<upload-token>
FIRMWARE_JOB_ID=<unique-safe-job-id>
```

The builder sends an authenticated HTTP `PUT` for the three firmware images,
`build.log`, and `manifest.json` to
`<upload-url>/<job-id>/artifacts/<filename>`. The manifest is uploaded last so
consumers do not observe a completed job before its other objects are available.
Transient connection, timeout, throttling, and server errors are retried up to
four times with exponential backoff; authentication and other permanent errors
fail immediately. Storage credentials and provider details remain entirely on
the receiving service.

Use a unique empty output directory for each job. In ECI, pass the same inputs
as container environment variables and let the receiver persist the output
after the process exits.

ESP-IDF uses Ninja, which automatically builds in parallel using the CPUs
visible to the container. Allocate at least 8 vCPUs to an ECI build job when
build latency is more important than compute cost; forcing a fixed `-j` value is
unnecessary and can oversubscribe smaller instances.

The production image targets `linux/arm64`. Create the ECI container group with
`CpuArchitecture=ARM64`, `Cpu=8`, and a memory size selected for the requested
board. The image architecture and ECI architecture must match.
