## What this changes

Describe the change and why it is needed.

## How it was verified

Describe what you actually ran, on what hardware. If a metric changed, say how
you confirmed the new number is correct rather than merely plausible.

## Checklist

- [ ] `mingw32-make clean && mingw32-make` completes with no warnings
- [ ] `python tools/check_style.py .` passes
- [ ] No comments added to `src/` or `include/`
- [ ] Includes are written as `#include "name.h"` with no relative path
- [ ] README updated if behaviour, configuration or requirements changed
