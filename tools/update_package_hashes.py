"""Fill the SHA256 placeholders in the package manifests (DIST-02, DIST-03).

Every channel manifest points at a GitHub release asset and carries a hash of
it, so none of them can be completed until that exact file is published. Doing
it by hand across three files with three different syntaxes is how they ended
up shipping all-zero placeholders against real download URLs.

Run after publishing a release:

    python tools/update_package_hashes.py v1.5.0

or against a local build, to check what the hash will be before uploading:

    python tools/update_package_hashes.py v1.5.0 --file build/kestrel.exe
"""

import argparse
import hashlib
import os
import re
import sys
import urllib.request

ASSET_URL = "https://github.com/semiloker/Kestrel/releases/download/{tag}/kestrel.exe"

# path -> (regex with one capturing group around the hash, human name)
TARGETS = [
    ("packaging/scoop/kestrel.json",
     r'("hash":\s*")([0-9a-fA-F]{64})(")', "scoop hash"),
    ("packaging/winget/semiloker.Kestrel.installer.yaml",
     r'(InstallerSha256:\s*)([0-9a-fA-F]{64})()', "winget InstallerSha256"),
    ("packaging/chocolatey/tools/chocolateyinstall.ps1",
     r"(checksum64\s*=\s*')([0-9a-fA-F]{64})(')", "chocolatey checksum64"),
]


def sha256_of_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_of_url(url):
    digest = hashlib.sha256()
    with urllib.request.urlopen(url) as response:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="release tag, e.g. v1.5.0")
    parser.add_argument("--file", help="hash this local file instead of downloading")
    parser.add_argument("--root", default=".", help="repository root")
    args = parser.parse_args()

    os.chdir(args.root)

    version = args.tag[1:] if args.tag.startswith("v") else args.tag

    if args.file:
        if not os.path.isfile(args.file):
            print("no such file: %s" % args.file)
            return 1
        digest = sha256_of_file(args.file)
        print("hashed %s" % args.file)
    else:
        url = ASSET_URL.format(tag=args.tag)
        print("downloading %s" % url)
        try:
            digest = sha256_of_url(url)
        except Exception as error:
            print("could not fetch the release asset: %s" % error)
            return 1

    print("sha256 %s" % digest)

    failures = []
    for path, pattern, name in TARGETS:
        if not os.path.isfile(path):
            failures.append("%s is missing" % path)
            continue

        text = open(path, encoding="utf-8").read()

        # A manifest still pointing at an older release would get a hash that
        # does not match its own download URL, which is worse than a placeholder.
        if version not in text:
            failures.append("%s does not mention version %s; bump it first" % (path, version))
            continue

        updated, count = re.subn(pattern, lambda m: m.group(1) + digest + m.group(3), text)
        if count != 1:
            failures.append("%s: expected one %s, found %d" % (path, name, count))
            continue

        if updated == text:
            print("  %-52s already current" % name)
            continue

        open(path, "w", encoding="utf-8", newline="").write(updated)
        print("  %-52s updated" % name)

    if failures:
        print("")
        for failure in failures:
            print(failure)
        return 1

    print("")
    print("All manifests carry the hash for %s." % args.tag)
    return 0


if __name__ == "__main__":
    sys.exit(main())
