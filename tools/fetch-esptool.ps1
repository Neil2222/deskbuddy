# Fetches the flashing tool the Companion bundles: Espressif's esptool v4.12.0 for Windows, and the sources that
# must travel with it (companion/third_party/esptool/README.md). Every download is checked against a pinned SHA-256;
# anything else is refused. Idempotent: files that are already there and match are kept.
#   tools/fetch-esptool.ps1
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$dest = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\companion\third_party\esptool'))
$exeSha = '1cd8386a7934862eb6ce1e14af3cc2309b0a743de1171fd09fab2f2ef2f5d1a3' # also in EsptoolBundle.ExeSha256
$downloads = @(
    @{ Url = 'https://github.com/espressif/esptool/releases/download/v4.12.0/esptool-v4.12.0-windows-amd64.zip'
       File = 'esptool-v4.12.0-windows-amd64.zip'; Sha = '42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889'; Keep = $false }
    @{ Url = 'https://github.com/espressif/esptool/archive/refs/tags/v4.12.0.tar.gz'
       File = 'esptool-4.12.0-source.tar.gz'; Sha = '0d099edae2d76edfeaed1b3c6699c278e168809e30845311431a0a812888ca83'; Keep = $true }
    @{ Url = 'https://github.com/espressif/esptool-legacy-flasher-stub/archive/refs/tags/v1.11.1.tar.gz'
       File = 'esptool-legacy-flasher-stub-1.11.1-source.tar.gz'; Sha = 'f056a3f717178c6ec6f66a4560e32b23d180c67119dbb9d1da774845b49c2bfc'; Keep = $true }
)

function Get-Sha([string] $Path) { (Get-FileHash -Algorithm SHA256 $Path).Hash.ToLower() }

$exe = Join-Path $dest 'esptool.exe'
$haveExe = (Test-Path $exe) -and ((Get-Sha $exe) -eq $exeSha)
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('deskbuddy-esptool-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $tmp | Out-Null
try {
    foreach ($d in $downloads) {
        $target = Join-Path $dest $d.File
        if ($d.Keep -and (Test-Path $target) -and ((Get-Sha $target) -eq $d.Sha)) { continue }
        if (-not $d.Keep -and $haveExe) { continue }
        $file = Join-Path $tmp $d.File
        Write-Host "Downloading $($d.Url)"
        Invoke-WebRequest -Uri $d.Url -OutFile $file -UseBasicParsing
        $sha = Get-Sha $file
        if ($sha -ne $d.Sha) { throw "$($d.File): SHA-256 $sha, expected $($d.Sha). Refusing it." }
        if ($d.Keep) {
            Copy-Item $file $target -Force
        } else {
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip = [IO.Compression.ZipFile]::OpenRead($file)
            try {
                $entry = $zip.Entries | Where-Object { $_.FullName -like '*/esptool.exe' } | Select-Object -First 1
                if (-not $entry) { throw 'esptool.exe not found in the release zip' }
                $out = Join-Path $tmp 'esptool.exe'
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $out, $true)
            } finally { $zip.Dispose() }
            $sha = Get-Sha $out
            if ($sha -ne $exeSha) { throw "esptool.exe: SHA-256 $sha, expected $exeSha. Refusing it." }
            Copy-Item $out $exe -Force
        }
    }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
if ((Get-Sha $exe) -ne $exeSha) { throw "$exe is not the pinned esptool.exe" }
Write-Host "esptool v4.12.0 ready in $dest (esptool.exe $exeSha)"
