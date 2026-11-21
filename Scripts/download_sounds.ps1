# Downloads free, high-quality SFZ sound libraries into Documents\KlankModule\Sounds.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\download_sounds.ps1
#
# Libraries:
#   - Salamander Grand Piano V3 (Yamaha C5), SFZ + FLAC, 48 kHz / 24 bit, ~707 MB
#     by Alexander Holm, Creative Commons Attribution 3.0 - https://freepats.zenvoid.org
#   - Optional (-WithVCSL): Versilian Community Sample Library, CC0 (public domain), large download
#     https://github.com/sgossner/VCSL

param(
    [switch]$WithVCSL
)

$ErrorActionPreference = "Stop"
$sounds = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "KlankModule\Sounds"
New-Item -ItemType Directory -Force -Path $sounds | Out-Null
$ProgressPreference = "SilentlyContinue"   # makes Invoke-WebRequest much faster

function Get-Archive($name, $url, $file) {
    $target = Join-Path $sounds $name
    if (Test-Path $target) {
        Write-Host "[=] $name staat er al ($target)"
        return
    }
    $archive = Join-Path $env:TEMP $file
    Write-Host "[v] $name downloaden..."
    Invoke-WebRequest -Uri $url -OutFile $archive
    Write-Host "[x] Uitpakken naar $target ..."
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    tar -xf $archive -C $target          # tar.exe is part of Windows 10/11
    Remove-Item $archive
    Write-Host "[ok] $name klaar"
}

Get-Archive "SalamanderGrandPiano" `
    "https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz" `
    "salamander.tar.gz"

if ($WithVCSL) {
    $target = Join-Path $sounds "VCSL"
    if (-not (Test-Path $target)) {
        $zip = Join-Path $env:TEMP "vcsl.zip"
        Write-Host "[v] VCSL downloaden (groot, even geduld)..."
        Invoke-WebRequest -Uri "https://github.com/sgossner/VCSL/archive/refs/heads/master.zip" -OutFile $zip
        Expand-Archive -Path $zip -DestinationPath $target
        Remove-Item $zip
        Write-Host "[ok] VCSL klaar"
    }
}

Write-Host ""
Write-Host "Klaar. Kies in KlankModule bij een part: Klank > SFZ-bibliotheek laden... en open een .sfz-bestand in:"
Write-Host "  $sounds"
