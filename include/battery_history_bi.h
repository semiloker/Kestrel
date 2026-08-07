#ifndef BATTERY_HISTORY_BI_H
#define BATTERY_HISTORY_BI_H

#include <windows.h>

class batteryinfo_bi;

// Appends a row to %APPDATA%\Kestrel\battery-history.csv on a slow interval.
// Nothing else in Kestrel records battery state over time: capture sessions
// sample power only while a recording is running, so wear, cycle count and
// charge have no history at all once the app closes.
class battery_history_bi
{
public:
    // Safe to call from the 1 s battery timer - it returns immediately until
    // the interval has elapsed, so the file work happens a few times an hour.
    void tick(const batteryinfo_bi *bi);

    void setEnabled(bool on) { enabled = on; }
    bool isEnabled() const { return enabled; }

    void setIntervalMinutes(int minutes);
    int getIntervalMinutes() const { return intervalMinutes; }

private:
    bool writeRow(const batteryinfo_bi *bi);

    bool enabled = true;
    int intervalMinutes = 5;
    ULONGLONG lastWriteTick = 0;
    bool wroteOnce = false;
    bool cappedLogged = false;
    bool openFailedLogged = false;
};

#endif
