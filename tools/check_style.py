import glob
import os
import re
import sys

SOURCE_GLOBS = ["src/*.cpp", "include/*.h"]
MAX_LINE = 180

# Note: the old "no comments" rule was dropped — the codebase now uses
# intentional documentation and rationale comments. DEV-06 tracks replacing
# this script with clang-format. Whitespace/line-length/guard checks remain.


def check_file(path, failures):
    with open(path, encoding="utf-8") as f:
        text = f.read()

    for idx, raw in enumerate(text.split("\n"), start=1):
        if raw.rstrip("\r") != raw.rstrip():
            failures.append("%s:%d: trailing whitespace" % (path, idx))
        if "\t" in raw:
            failures.append("%s:%d: tab character" % (path, idx))
        if len(raw.rstrip("\r")) > MAX_LINE:
            failures.append("%s:%d: line longer than %d characters" % (path, idx, MAX_LINE))

    for m in re.finditer(r'#include\s+"([^"]+)"', text):
        target = m.group(1)
        if "/" in target or "\\" in target:
            failures.append('%s: include "%s" must not contain a path' % (path, target))

    if text and not text.endswith("\n"):
        failures.append("%s: file does not end with a newline" % path)


def check_headers_guarded(failures):
    for path in glob.glob("include/*.h"):
        with open(path, encoding="utf-8") as f:
            head = f.read(400)
        if "#ifndef" not in head and "#pragma once" not in head:
            failures.append("%s: missing include guard" % path)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    os.chdir(root)

    failures = []
    files = []
    for pattern in SOURCE_GLOBS:
        files.extend(sorted(glob.glob(pattern)))

    if not files:
        print("no source files found")
        return 1

    for path in files:
        check_file(path.replace("\\", "/"), failures)

    check_headers_guarded(failures)

    if failures:
        for f in failures:
            print(f)
        print("")
        print("%d style problem(s) in %d file(s)" % (len(failures), len(files)))
        return 1

    print("style check passed: %d files" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
