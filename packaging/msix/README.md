# MSIX packaging

Wraps the portable `kestrel.exe` as a full-trust MSIX (`Windows.FullTrustApplication`).

## What you must supply
- **Store logos** in `assets/`: `StoreLogo.png` (50x50), `Square150x150Logo.png`,
  `Square44x44Logo.png`. Generate from `../../assets/kestrel.ico` or the logo PNG.
- **A signing certificate** whose subject matches `Publisher="CN=<publisher-id>"`
  in `AppxManifest.xml` (for local install/testing only). `Identity.Name` and
  `Publisher` are already filled in from Partner Center > "Kestrel App" (MSIX
  product) > View product identity. For the **Microsoft Store**, submit the
  *unsigned* package — the Store re-signs it with these same identity values.

## Local build & test
```powershell
# self-signed cert for testing (subject must match the manifest Publisher)
New-SelfSignedCertificate -Type Custom -Subject "CN=35C83A06-0225-4530-A088-4C0C0BBBF674" `
  -KeyUsage DigitalSignature -CertStoreLocation "Cert:\CurrentUser\My" `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")

powershell -File build-msix.ps1 -ExePath ..\..\build\bin\kestrel.exe `
  -PfxPath mycert.pfx -PfxPassword pw

Add-AppxPackage .\Kestrel.msix   # install locally
```

## Store submission
1. `build-msix.ps1` (no `-PfxPath`) to produce an unsigned `.msix`.
2. Upload it on the Packages page of the "Kestrel App" submission in Partner
   Center, fill in the Store listing (description, screenshots, age ratings),
   and submit for certification.
