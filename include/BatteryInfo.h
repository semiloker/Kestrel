#ifndef BATTERYINFO_H
#define BATTERYINFO_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <pdh.h>
#include <algorithm>
#include <map>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <initguid.h>
#include <setupapi.h>
#include <batclass.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <comdef.h>
#include <Wbemidl.h>

#include "interfaces_bi.h"

template<typename T>
T clamp(T value, T min, T max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

DEFINE_GUID(GUID_DEVINTERFACE_BATTERY,
0x72631e54, 0x78a4, 0x11d0, 0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a);

class batteryinfo_bi : public IBatteryInfo
{
private:
    HDEVINFO hDevInfo;
    HANDLE hBattery;
    BATTERY_INFORMATION bi{};
    BATTERY_STATUS bs{};
    ULONG tag;
    ULONGLONG lastRecoverTick = 0;
    // Cleared the first time the pack refuses to report a temperature, so the
    // 1 s timer stops asking. Recover() re-arms it: a genuine re-enumeration
    // can bring a sensor back.
    bool tempSupported = true;

    bool OpenDevice();
    bool Recover();
    // Both battery IOCTL input structs start with the tag, so one wrapper can
    // stamp it, retry after a recovery, and keep the callers unchanged.
    bool TaggedIoctl(DWORD code, void *in, DWORD inSize, void *out, DWORD outSize);
    // Same call without the recovery retry. An optional info level the firmware
    // never implemented is not a stale tag, and treating it as one puts
    // Recover() on a permanent 5 s loop.
    bool PlainIoctl(DWORD code, void *in, DWORD inSize, void *out, DWORD outSize);
    // The name/serial info levels all return a plain wide string; only the
    // level and the destination differ.
    bool QueryInfoString(ULONG level, std::string *out);

public:
    using bi_struct_static = IBatteryInfo::bi_struct_static;
    using bi_struct_dynamic_1s = IBatteryInfo::bi_struct_dynamic_1s;
    using bi_struct_dynamic_10s = IBatteryInfo::bi_struct_dynamic_10s;

    bi_struct_static info_static;
    bi_struct_dynamic_1s info_1s;
    bi_struct_dynamic_10s info_10s;

    bool present = false;

    batteryinfo_bi() : hDevInfo(INVALID_HANDLE_VALUE), hBattery(INVALID_HANDLE_VALUE), tag(0)
    {
    }
    batteryinfo_bi(const batteryinfo_bi &) = delete;
    batteryinfo_bi &operator=(const batteryinfo_bi &) = delete;

    ~batteryinfo_bi()
    {
        if (hBattery != INVALID_HANDLE_VALUE)
            CloseHandle(hBattery);
        if (hDevInfo != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    bool Initialize() override;
    bool QueryTag() override;
    bool QueryBatteryInfo() override;
    bool QueryBatteryStatus() override;
    bool QueryBatteryRemaining() override;
    bool QueryBatteryCycleCount() override;
    bool QueryBatteryTemperature() override;
    bool QueryBatteryIdentity() override;

    void PrintAllConsole() const;
};

#endif
