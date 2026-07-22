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

#ifndef BatteryCycleCount
    #define BatteryCycleCount ((BATTERY_QUERY_INFORMATION_LEVEL)6)
#endif

template<typename T>
T clamp(T value, T min, T max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

DEFINE_GUID(GUID_DEVINTERFACE_BATTERY,
0x72631e54, 0x78a4, 0x11d0, 0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a);

DEFINE_GUID(GUID_BATTERY_WMI_CYCLE_COUNT,
    0x2a2d7d6d, 0x8f1f, 0x457c, 0x9e, 0x1c, 0x3d, 0x7a, 0x2c, 0x91, 0x1d, 0x28);

typedef struct _BATTERY_WMI_CYCLE_COUNT
{
    ULONG Tag;
    ULONG CycleCount;
} BATTERY_WMI_CYCLE_COUNT, *PBATTERY_WMI_CYCLE_COUNT;

class batteryinfo_bi
{
private:
    HDEVINFO hDevInfo;
    HANDLE hBattery;
    BATTERY_INFORMATION bi{};
    BATTERY_STATUS bs{};
    ULONG tag;

public:
    struct bi_struct_static
    {
        std::string Chemistry;
        std::string DesignedCapacity;
        std::string FullChargedCapacity;
        std::string DefaultAlert1;
        std::string DefaultAlert2;
        std::string WearLevel;
        std::string CycleCount;

        double designedWh = 0.0;
        double fullChargedWh = 0.0;
        double alert1Wh = 0.0;
        double alert2Wh = 0.0;
        double wearPercent = 0.0;
        int cycleCount = 0;

        bool capacityValid = false;
        bool wearValid = false;
        bool cycleCountValid = false;
        bool alertsValid = false;
    };

    struct bi_struct_dynamic_1s
    {
        std::string Voltage;
        std::string Rate;
        std::string PowerState;
        std::string RemainingCapacity;
        std::string ChargeLevel;

        double voltageV = 0.0;
        double rateW = 0.0;
        double remainingWh = 0.0;
        double chargePercent = 0.0;

        bool voltageValid = false;
        bool rateValid = false;
        bool remainingValid = false;
        bool chargeValid = false;

        bool charging = false;
        bool discharging = false;
        bool onLine = false;

        bool Voltage_ = false;
        bool Rate_ = false;
        bool PowerState_ = false;
        bool RemainingCapacity_ = false;
        bool ChargeLevel_ = false;
    };

    struct bi_struct_dynamic_10s
    {
        std::string TimeRemaining;
        std::string TimeToFullCharge;

        int minutesToEmpty = -1;
        int minutesToFull = -1;

        bool TimeRemaining_ = false;
    };

    bi_struct_static info_static;
    bi_struct_dynamic_1s info_1s;
    bi_struct_dynamic_10s info_10s;

    bool present = false;

    batteryinfo_bi() : hDevInfo(INVALID_HANDLE_VALUE), hBattery(INVALID_HANDLE_VALUE), tag(0)
    {
    }

    ~batteryinfo_bi()
    {
        if (hBattery != INVALID_HANDLE_VALUE)
            CloseHandle(hBattery);
        if (hDevInfo != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    bool Initialize();
    bool QueryTag();
    bool QueryBatteryInfo();
    bool QueryBatteryStatus();
    bool QueryBatteryRemaining();
    bool QueryBatteryCycleCount();

    void PrintAllConsole() const;
};

#endif
