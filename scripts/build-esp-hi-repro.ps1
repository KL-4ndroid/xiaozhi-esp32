[CmdletBinding()]
param(
    [Parameter()]
    [string]$OutputDirectory = "repro-build-esp-hi",

    [Parameter()]
    [string]$Language = "zh-CN",

    [Parameter()]
    [string]$WakeWord = "nihaoxiaozhi"
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputPath = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputDirectory))
}

if (Test-Path -LiteralPath $outputPath) {
    if (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1) {
        throw "Output directory must be empty: $outputPath"
    }
} else {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

Push-Location $repositoryRoot
try {
    $sourceRevision = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to read the firmware source revision."
    }
    if (& git status --porcelain) {
        $sourceRevision = "$sourceRevision-dirty"
    }

    $image = "xiaozhi/esp-hi-builder:idf-v6.1"
    & docker build `
        --build-arg "FIRMWARE_SOURCE_REVISION=$sourceRevision" `
        -f docker/firmware-builder/Dockerfile `
        -t $image `
        .
    if ($LASTEXITCODE -ne 0) {
        throw "Builder image creation failed."
    }

    $outputMount = "type=bind,source=$outputPath,target=/output"
    & docker run --rm `
        -e FIRMWARE_BOARD_DIR=espressif/esp-hi `
        -e FIRMWARE_BOARD_NAME=esp-hi `
        -e "FIRMWARE_LANGUAGE=$Language" `
        -e "FIRMWARE_WAKE_WORD=$WakeWord" `
        --mount $outputMount `
        $image
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-Hi firmware build failed. See $outputPath\build.log"
    }
} finally {
    Pop-Location
}

Write-Host "Firmware build completed: $outputPath"
