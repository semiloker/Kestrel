$ErrorActionPreference = 'Stop'
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Portable: drop kestrel.exe into the package tools dir; choco shims it onto PATH.
$packageArgs = @{
  packageName  = 'kestrel'
  url64bit     = 'https://github.com/semiloker/Kestrel/releases/download/v1.5.0/kestrel.exe'
  # Fill in after the release asset is uploaded (Get-FileHash -Algorithm SHA256):
  checksum64   = '73cefaf497e414457fe57a5e42d2c2885552075babd7ce044418acc2c3b137a1'
  checksumType64 = 'sha256'
  fileFullPath = Join-Path $toolsDir 'kestrel.exe'
}

Get-ChocolateyWebFile @packageArgs
