$ErrorActionPreference = 'Stop'
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Portable: drop kestrel.exe into the package tools dir; choco shims it onto PATH.
$packageArgs = @{
  packageName  = 'kestrel'
  url64bit     = 'https://github.com/semiloker/Kestrel/releases/download/v1.4.0/kestrel.exe'
  # Fill in after the release asset is uploaded (Get-FileHash -Algorithm SHA256):
  checksum64   = '0000000000000000000000000000000000000000000000000000000000000000'
  checksumType64 = 'sha256'
  fileFullPath = Join-Path $toolsDir 'kestrel.exe'
}

Get-ChocolateyWebFile @packageArgs
