# Packaging & distribution

Manifests for every distribution channel in the roadmap. All reference a GitHub
release asset `kestrel.exe` at tag `v<version>`. **Each has a `0000…` SHA256
placeholder you must fill in after uploading the release asset** — nothing here
can be finished until a signed binary is published, because the hash is computed
from that exact file.

| Channel | Files | What you still need |
|---------|-------|---------------------|
| Winget (DIST-02) | `winget/*.yaml` | Real `InstallerSha256`; submit PR to microsoft/winget-pkgs |
| Scoop (DIST-03) | `scoop/kestrel.json` | Real `hash`; host in a bucket repo |
| Chocolatey (DIST-03) | `chocolatey/` | Real `checksum64`; `choco pack` + `choco push` |
| MSIX / Store (DIST-04) | `msix/` | Store account; logos; run `build-msix.ps1` |
| Code signing (DIST-01) | `../.github/workflows/ci.yml` | A code-signing cert in repo secrets |

## Compute a hash

```powershell
Get-FileHash kestrel.exe -Algorithm SHA256   # PowerShell
sha256sum kestrel.exe                          # bash
```

## Winget

```powershell
winget validate --manifest packaging/winget
winget install --manifest packaging/winget    # local test
```
Then open a PR against [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)
under `manifests/s/semiloker/Kestrel/1.4.0/`.

## Scoop

Put `kestrel.json` in a bucket repo (e.g. `semiloker/scoop-bucket`), then:
```powershell
scoop bucket add kestrel https://github.com/semiloker/scoop-bucket
scoop install kestrel
```

## Chocolatey

```powershell
cd packaging/chocolatey
# copy the release kestrel.exe into tools/ OR keep the Get-ChocolateyWebFile URL
choco pack
choco push kestrel.1.4.0.nupkg --source https://push.chocolatey.org/
```

## MSIX / Microsoft Store

MSIX contradicts the "zero-install portable" pitch (it installs into the
packaged-app store, not a folder you can copy to a USB stick) — offer it
*alongside* the portable .exe, not as a replacement. See `msix/README.md`.

```powershell
cd packaging/msix
powershell -File build-msix.ps1 -ExePath ..\..\build\bin\kestrel.exe
```

## Code signing (DIST-01)

The CI workflow has an optional signing step gated on the `SIGNING_CERT_PFX`
secret (base64 of your .pfx) and `SIGNING_CERT_PASSWORD`. Until you add a
certificate it is skipped and the release ships unsigned (SmartScreen warns).
An OV/EV cert costs money and requires identity verification — only you can
obtain it.
