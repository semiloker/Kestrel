# Changelog

Notable changes per release. Dates are the release tag dates; before 1.5.0 the
commit messages were the only release notes, so those entries are summaries
rather than complete lists.

## 1.5.0 - unreleased

### Added

- Battery temperature, read from the pack's own sensor, in the Battery tab and
  as a `Bat:` overlay metric sharing the temperature axis with CPU and GPU.
  Firmware that does not implement the sensor reports it as unavailable rather
  than showing a number.
- Battery identity: manufacturer, model, serial and manufacture date, with the
  pack's age derived from the date, in a new BATTERY card.
- GPU fan speed as a `GPU: RPM` overlay metric and a Fan speed toggle. The
  driver was already returning this on every poll and it was being discarded.
- `battery-history.csv` in the data directory: charge, capacity, wear, cycle
  count, temperature and rate appended every few minutes, so battery ageing has
  a record. Controlled by `battery.history` and `battery.historyMinutes`.

### Changed

- Overlay labels name the chip instead of abbreviating the quantity: GPU watts
  and GPU temperature are both `GPU:`, CPU temperature is `CPU:`. The unit
  already distinguishes them, and `GPU:` was in use for frame time.
- Thermal and fan data is read from every physical adapter behind a LUID rather
  than index 0 only, so linked and hybrid adapters report.

### Fixed

- A saved overlay metric order is no longer discarded when a new metric is
  added, which silently reset every custom arrangement on upgrade.
- Capture finalization failures are logged and shown instead of the run
  vanishing from history with nothing to explain it.
- Battery readings show a dash instead of freezing on their last values when a
  query fails permanently. 1.4.4 fixed the retry but the caller ignored it.
- Network speeds no longer publish a measured-looking 0.0 KB/s for an interface
  that has no previous sample to compare against.
- Optional battery info levels no longer drive the device-tag recovery path,
  which put firmware without a temperature sensor into a permanent 5 second
  re-enumeration loop.
- Two WMI cycle-count failures now log, like the others already did.

### Removed

- The disabled settings search bar and the five orphaned sites it stranded.
- Dead battery cycle-count declarations left over from an IOCTL approach that
  WMI replaced, including a macro that shadowed a real SDK info level.

## 1.4.4 - 2026-08-05

Fixed stalled battery readings after suspend or resume, and an elevated restart
that fell back to a UAC prompt. Fixed Open Folder pointing at an empty path
under MSIX.

## 1.4.3 - 2026-07-28

Fixed captures silently producing nothing without elevation. Added MSIX
packaging for the Microsoft Store.

## 1.4.2 - 2026-07-26

Fixed release gate failures surfaced by the security hardening merge.

## 1.4.1 - 2026-07-26

Tray icon recovery after an Explorer restart, real temperature sources, and
overlay fixes.

## 1.4.0 - 2026-07-23

Architecture refactor, additional sensors, i18n string tables, the first-run
wizard, distribution packaging, and UI fixes.

## 1.3.6 - 2026-07-23

Relaunch through the scheduled task only when it points at this executable.

## 1.3.5 - 2026-07-23

Earlier releases are recorded in the git history and the GitHub releases page.
