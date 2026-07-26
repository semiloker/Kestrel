#ifndef MAHM_SENSOR_BI_H
#define MAHM_SENSOR_BI_H

#include <windows.h>

// Optional CPU temperature source: the shared-memory block MSI Afterburner
// publishes while it is running.
//
// Windows exposes no die temperature to unprivileged code - the digital thermal
// sensor lives behind an MSR that only ring 0 can read. ACPI thermal zones are
// the documented user-mode substitute, but on desktops they usually describe a
// board zone that barely moves with load. Afterburner already runs a signed
// driver for exactly this, so when it is present we read its value instead of
// shipping a driver of our own.
//
// Nothing is required of the user: if Afterburner is not running the reader
// stays silent and the ACPI zone remains the fallback.
class mahm_sensor_bi
{
public:
    mahm_sensor_bi() = default;
    ~mahm_sensor_bi();

    mahm_sensor_bi(const mahm_sensor_bi &) = delete;
    mahm_sensor_bi &operator=(const mahm_sensor_bi &) = delete;

    // False when Afterburner is absent, idle, or its block has gone stale.
    bool readCpuTemperature(double *celsiusOut);

private:
    bool ensureMapping();
    void closeMapping();

    HANDLE map_ = NULL;
    const BYTE *view_ = NULL;
    DWORD lastOpenAttempt_ = 0;
    bool logged_ = false;
};

#endif
