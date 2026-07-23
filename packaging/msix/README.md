# MSIX packaging

Wraps the portable `kestrel.exe` as a full-trust MSIX (`Windows.FullTrustApplication`).

## What you must supply
- **Store logos** in `assets/`: `StoreLogo.png` (50x50), `Square150x150Logo.png`,
  `Square44x44Logo.png`. Generate from `../../assets/kestrel.ico` or the logo PNG.
- **A signing certificate** whose subject matches `Publisher="CN=semiloker"` in
  `AppxManifest.xml` (for local install/testing). For the **Microsoft Store**,
  submit the *unsigned* package — the Store re-signs it, and it assigns the real
  `Publisher`/`Identity.Name`, which must then match what you reserved in
  Partner Center.

## Local build & test
```powershell
# self-signed cert for testing (subject must match the manifest Publisher)
New-SelfSignedCertificate -Type Custom -Subject "CN=semiloker" `
  -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")

powershell -File build-msix.ps1 -ExePath ..\..\build\bin\kestrel.exe `
  -PfxPath mycert.pfx -PfxPassword pw

Add-AppxPackage .\Kestrel.msix   # install locally
```

## Store submission
1. Reserve the name "Kestrel" in Partner Center.
2. Set `Identity.Name`/`Publisher` in `AppxManifest.xml` to the values Partner
   Center shows for your reserved app.
3. `build-msix.ps1` (no `-PfxPath`) to produce an unsigned `.msix`.
4. Upload in Partner Center.
