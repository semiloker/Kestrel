# Builds an MSIX package around the portable kestrel.exe.
# Requires the Windows SDK (makeappx.exe, signtool.exe on PATH).
#
#   powershell -File build-msix.ps1 -ExePath ..\..\build\bin\kestrel.exe
#
# For local testing you can sign with a self-signed cert whose subject matches
# the Publisher in AppxManifest.xml (CN=semiloker). For the Microsoft Store,
# the Store re-signs the package, so you submit the unsigned .msix.

param(
    [string]$ExePath = "..\..\build\bin\kestrel.exe",
    [string]$Manifest = "AppxManifest.xml",
    [string]$OutFile = "Kestrel.msix",
    [string]$PfxPath = "",          # optional: path to signing .pfx
    [string]$PfxPassword = ""
)

$ErrorActionPreference = "Stop"
$stage = Join-Path $PSScriptRoot "stage"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "assets") | Out-Null

Copy-Item $ExePath (Join-Path $stage "kestrel.exe")
Copy-Item (Join-Path $PSScriptRoot $Manifest) (Join-Path $stage "AppxManifest.xml")

# Store logos: reuse the app icon PNGs if present, else placeholders must be added.
$logos = @("StoreLogo.png", "Square150x150Logo.png", "Square44x44Logo.png")
foreach ($logo in $logos) {
    $src = Join-Path $PSScriptRoot "assets\$logo"
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage "assets\$logo") }
    else { Write-Warning "Missing $logo - add packaging/msix/assets/$logo before Store submission." }
}

$out = Join-Path $PSScriptRoot $OutFile
& makeappx.exe pack /d $stage /p $out /overwrite
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }

if ($PfxPath -ne "") {
    & signtool.exe sign /fd SHA256 /a /f $PfxPath /p $PfxPassword $out
    if ($LASTEXITCODE -ne 0) { throw "signtool failed" }
    Write-Host "Signed $out"
} else {
    Write-Host "Built (unsigned) $out - sign it or submit to the Store as-is."
}
