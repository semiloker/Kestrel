$ErrorActionPreference = 'Stop'
$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Portable: drop kestrel.exe into the package tools dir; choco shims it onto PATH.
$packageArgs = @{
  packageName  = 'kestrel'
  url64bit     = 'https://github.com/semiloker/Kestrel/releases/download/v1.6.0/kestrel.exe'
  # Fill in after the release asset is uploaded (Get-FileHash -Algorithm SHA256):
  checksum64   = '1f5f5cf56d348adae4ac014bc678c705be01717a4c4d128b80a8f49fee29ed5e'
  checksumType64 = 'sha256'
  fileFullPath = Join-Path $toolsDir 'kestrel.exe'
}

Get-ChocolateyWebFile @packageArgs
