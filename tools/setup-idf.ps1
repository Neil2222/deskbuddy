# One-time toolchain install: clones the pinned ESP-IDF and installs the ESP32-S3 tools.
# Prerequisites: Git and Python 3.9-3.13 on PATH.
#   tools/setup-idf.ps1                       # installs to %USERPROFILE%\esp\<version>\esp-idf
#   $env:DESKBUDDY_IDF_PATH='D:\esp\idf'; tools/setup-idf.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$idf = Get-IdfPath
if (-not (Test-Path $idf)) {
    git clone -b $IdfVersion --depth 1 --recursive --shallow-submodules -j 8 https://github.com/espressif/esp-idf.git $idf
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& (Join-Path $idf 'install.ps1') esp32s3
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Enter-Idf
# Call idf.py through Python: the idf.py.exe launcher on PATH answers --version with its own version.
python (Join-Path $idf 'tools\idf.py') --version
