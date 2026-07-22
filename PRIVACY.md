# Privacy Policy for Kestrel

**Last updated: 22 July 2026**

Kestrel is a battery, system and frame-time overlay for Windows, published by
**semiloker**. This policy explains exactly what the application reads, what it
stores, and what leaves your computer.

The short version: **Kestrel does not collect, transmit or sell any personal
data.** It has no accounts, no analytics, no advertising, and no third-party
tracking components. Everything it measures stays on your machine.

---

## 1. Data Kestrel reads

Kestrel reads hardware and operating-system counters in order to display them.
This information is read live, held in memory, and discarded when the
application closes.

| What is read | Where it comes from | Why |
| --- | --- | --- |
| Battery chemistry, capacity, charge level, wear, cycle count, voltage, charge/discharge rate | Windows battery driver (`IOCTL_BATTERY_*`), WMI class `BatteryCycleCount` | To display battery status and health |
| CPU load, per-core load, processor name, architecture | Windows Performance Counters (PDH), `GetSystemInfo` | To display processor usage |
| CPU package power | Windows Energy Meter counter set | To display power draw |
| GPU load, adapter name, per-process GPU time | Windows Performance Counters (PDH) | To display graphics usage |
| Physical, commit and virtual memory figures | `GlobalMemoryStatusEx` | To display memory usage |
| Frame rate, frame interval, per-frame GPU time | Event Tracing for Windows (ETW), the same event stream used by Intel PresentMon | To display frame timing for the application in the foreground |
| The window handle, process ID and executable name of the foreground application | `GetForegroundWindow`, `GetWindowThreadProcessId` | To attribute frame timing to the correct application |
| Display resolution, refresh rate and DPI | Windows display APIs | To label the overlay |

Kestrel does **not** read your files, documents, browsing history, keystrokes,
clipboard, camera, microphone, location, or the contents of any other
application. It does not take screenshots.

Frame timing requires administrator rights. If Kestrel is not elevated, these
values are simply unavailable and are shown as dashes.

## 2. Data Kestrel stores on your computer

Kestrel is a portable application. It writes two files, both inside your own
user profile at `%APPDATA%\Kestrel\`:

- **`settings.ini`** — your preferences: which metrics are shown, overlay
  position and margin, accent colour, theme, and startup behaviour. It contains
  no personal information.
- **`kestrel.log`** — a diagnostic log. It may record hardware identifiers such
  as your CPU model, GPU adapter name, and battery chemistry, together with
  error messages. It exists so that you can see why a value could not be read.
- **`captures\`** — created only when you record a session. Each run writes a
  CSV of frame times plus an `index.csv` summarising the runs. These files
  contain performance measurements and the name of the program you recorded.
  Nothing is uploaded; delete the folder at any time.

Both files stay on your computer. Neither is transmitted anywhere. You may
delete either file at any time; deleting `settings.ini` resets Kestrel to its
defaults.

If you enable **Start with Windows**, Kestrel creates a Windows Task Scheduler
entry or a `Run` registry value so that Windows can launch it. Disabling the
setting removes it.

## 3. Network connections

Kestrel makes **no network connections at all** during normal use. It does not
phone home, does not check licences, and does not send usage statistics.

The single exception is the update feature, which runs **only when you press
"Check for updates" or "Download" on the About tab.** Kestrel never checks for
updates on its own and never downloads anything in the background.

When you press those buttons, Kestrel makes an ordinary HTTPS request to:

- `api.github.com` — to read the latest published release number
- `objects.githubusercontent.com` (or another GitHub download host) — to
  download the new program file

As with any web request, GitHub receives your IP address and the request's
user-agent string (`Kestrel-Updater`). Kestrel sends **no** identifiers, no
account information, no hardware details and no usage data. GitHub's handling of
that request is governed by the
[GitHub Privacy Statement](https://docs.github.com/site-policy/privacy-policies/github-privacy-statement).

If you never press those buttons, Kestrel never contacts the network.

## 4. Data sharing

Kestrel shares no data with anyone. There are no third-party SDKs, advertising
networks, analytics providers, or crash-reporting services embedded in the
application. No data is sold, rented, or disclosed to any party.

## 5. Children's privacy

Kestrel is a general-purpose system utility. It does not knowingly collect any
information from anyone, including children under 13.

## 6. Your control over your data

Because all data stays on your machine, you retain full control:

- Delete `%APPDATA%\Kestrel\` to remove all stored settings and logs.
- Uninstalling or deleting the application removes the program itself.
- Turn off any metric you do not want displayed on the Settings tab.

There is no server-side account to delete, because no account exists.

## 7. Security

Kestrel runs as a normal user process and installs no kernel driver, service, or
background agent. It requests administrator rights only when you explicitly ask
for frame timing, and only to open an ETW trace session.

Update downloads are made over HTTPS. Before installing an update, Kestrel
verifies that the downloaded file is a valid Windows executable and keeps your
previous version as `kestrel.old.exe` so that you can roll back.

## 8. Changes to this policy

If this policy changes, the revised version will be published in the Kestrel
repository with an updated date at the top of this document.

## 9. Contact

Questions about this policy or about Kestrel's behaviour:

- GitHub issues: <https://github.com/semiloker/Kestrel/issues>
- Repository: <https://github.com/semiloker/Kestrel>

---

*Kestrel is free software released under the MIT Licence. Its complete source
code is public, so every claim in this document can be verified by reading it.*
