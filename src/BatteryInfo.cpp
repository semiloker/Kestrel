#include "BatteryInfo.h"
#include "logger_bi.h"

#include <climits>
#include <format>

namespace
{
    // ponytail: fixed backoff between recovery attempts. Cheap enough on a
    // machine with no battery at all (one SetupDi enumeration every 5 s);
    // make it exponential only if that ever shows up in a profile.
    const ULONGLONG RECOVER_INTERVAL_MS = 5000;
}

bool batteryinfo_bi::OpenDevice()
{
    if (hBattery != INVALID_HANDLE_VALUE)
        CloseHandle(hBattery);
    hBattery = INVALID_HANDLE_VALUE;

    if (hDevInfo != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(hDevInfo);

    hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_BATTERY, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hDevInfo == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVICE_INTERFACE_DATA did = {};
    did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_BATTERY, 0, &did))
        return false;

    DWORD size = 0;
    SetupDiGetDeviceInterfaceDetail(hDevInfo, &did, NULL, 0, &size, NULL);

    PSP_DEVICE_INTERFACE_DETAIL_DATA detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(size);
    if (!detail)
        return false;

    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &did, detail, size, NULL, NULL))
    {
        free(detail);
        return false;
    }

    hBattery = CreateFile(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    free(detail);

    return hBattery != INVALID_HANDLE_VALUE;
}

bool batteryinfo_bi::Initialize()
{
    if (!OpenDevice())
        return false;

    present = QueryTag() && QueryBatteryInfo() && QueryBatteryStatus() && QueryBatteryRemaining();

    QueryBatteryCycleCount();

    return present;
}

// Windows invalidates the battery tag across suspend/resume, a driver reload or
// a battery re-enumeration, and hands out a stale device handle when the whole
// interface is torn down. Both leave every later IOCTL failing, which used to
// freeze the readings on their last good values for as long as the app stayed
// up - the "sitting in the tray all day and the numbers stopped moving" bug.
bool batteryinfo_bi::Recover()
{
    ULONGLONG now = GetTickCount64();
    if (now - lastRecoverTick < RECOVER_INTERVAL_MS)
        return false;
    lastRecoverTick = now;

    bool ok = QueryTag() || (OpenDevice() && QueryTag());
    if (!ok)
        return false;

    log_bi::write("battery: re-acquired the device tag after a failed query");

    // Covers the battery that is not enumerated yet when we start at logon:
    // Initialize() failed, and these are the only values it would have filled.
    present = true;
    if (!info_static.cycleCountValid)
        QueryBatteryCycleCount();

    return true;
}

static_assert(offsetof(BATTERY_QUERY_INFORMATION, BatteryTag) == 0 &&
                  offsetof(BATTERY_WAIT_STATUS, BatteryTag) == 0,
              "TaggedIoctl stamps the tag over the first ULONG of its input");

bool batteryinfo_bi::TaggedIoctl(DWORD code, void *in, DWORD inSize,
                                 void *out, DWORD outSize)
{
    DWORD returned = 0;
    *(ULONG *)in = tag;

    if (DeviceIoControl(hBattery, code, in, inSize, out, outSize, &returned, NULL))
        return true;

    if (!Recover())
        return false;

    *(ULONG *)in = tag;
    returned = 0;
    return DeviceIoControl(hBattery, code, in, inSize, out, outSize,
                           &returned, NULL) != FALSE;
}

bool batteryinfo_bi::QueryTag()
{
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_TAG,
                         NULL, 0, &tag, sizeof(tag), &bytesReturned, NULL) ||
        tag == 0)
        return false;
    return true;
}

bool batteryinfo_bi::QueryBatteryCycleCount()
{
    HRESULT hres;

    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres))
    {
        info_static.CycleCount = "COM init failed";
        return false;
    }

    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);

    if (FAILED(hres) && hres != RPC_E_TOO_LATE)
    {
        info_static.CycleCount = "Security init failed";
        CoUninitialize();
        return false;
    }

    IWbemLocator *pLoc = nullptr;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID *)&pLoc);

    if (FAILED(hres))
    {
        info_static.CycleCount = "WMI locator failed";
        CoUninitialize();
        return false;
    }

    IWbemServices *pSvc = nullptr;
    BSTR resource = SysAllocString(L"ROOT\\WMI");
    hres = pLoc->ConnectServer(
        resource, NULL, NULL, NULL,
        0, NULL, NULL, &pSvc);
    SysFreeString(resource);

    if (FAILED(hres))
    {
        info_static.CycleCount = "WMI connect failed";
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    hres = CoSetProxyBlanket(pSvc,
                             RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                             NULL, EOAC_NONE);

    if (FAILED(hres))
    {
        info_static.CycleCount = "WMI proxy failed";
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    IEnumWbemClassObject *pEnumerator = nullptr;
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT * FROM BatteryCycleCount");
    hres = pSvc->ExecQuery(
        language, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator);
    SysFreeString(language);
    SysFreeString(query);

    if (FAILED(hres) || !pEnumerator)
    {
        info_static.CycleCount = "Query failed";
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject *pClassObject = nullptr;
    ULONG uReturn = 0;

    HRESULT nextResult = pEnumerator->Next(2000, 1, &pClassObject, &uReturn);
    if (nextResult == S_OK && uReturn == 1 && pClassObject)
    {
        VARIANT vtProp;
        VariantInit(&vtProp);

        hres = pClassObject->Get(L"CycleCount", 0, &vtProp, 0, 0);

        if (SUCCEEDED(hres) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4))
        {
            std::ostringstream oss;
            oss << vtProp.uintVal << " cycles";
            info_static.CycleCount = oss.str();
            info_static.cycleCount = (int)vtProp.uintVal;
            info_static.cycleCountValid = true;
        }
        else
        {
            info_static.CycleCount = "Unsupported";
            log_bi::write("cycle count: firmware does not expose the CycleCount property");
        }

        VariantClear(&vtProp);
        pClassObject->Release();
    }
    else
    {
        info_static.CycleCount =
            nextResult == WBEM_S_TIMEDOUT ? "Timed out" : "No data";
        log_bi::write("cycle count: ROOT\\WMI BatteryCycleCount returned no data (0x%08lX)",
                      (unsigned long)nextResult);
    }

    pEnumerator->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();

    return true;
}

bool batteryinfo_bi::QueryBatteryInfo()
{
    BATTERY_QUERY_INFORMATION bqi = {};
    bqi.InformationLevel = BatteryInformation;

    if (!TaggedIoctl(IOCTL_BATTERY_QUERY_INFORMATION,
                     &bqi, sizeof(bqi), &bi, sizeof(bi)))
        return false;

    info_static.Chemistry = std::string((char *)bi.Chemistry, 4);

    bool relative = (bi.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0;

    info_static.capacityValid = !relative && bi.DesignedCapacity > 0 && bi.FullChargedCapacity > 0;
    info_static.designedWh = bi.DesignedCapacity / 1000.0;
    info_static.fullChargedWh = bi.FullChargedCapacity / 1000.0;
    info_static.alert1Wh = bi.DefaultAlert1 / 1000.0;
    info_static.alert2Wh = bi.DefaultAlert2 / 1000.0;
    info_static.alertsValid = !relative && (bi.DefaultAlert1 > 0 || bi.DefaultAlert2 > 0);

    if (info_static.capacityValid)
    {
        info_static.DesignedCapacity = std::format("{:.1f} Wh", info_static.designedWh);
        info_static.FullChargedCapacity = std::format("{:.1f} Wh", info_static.fullChargedWh);
        info_static.DefaultAlert1 = std::format("{:.1f} Wh", info_static.alert1Wh);
        info_static.DefaultAlert2 = std::format("{:.1f} Wh", info_static.alert2Wh);
    }
    else
    {
        info_static.DesignedCapacity = "Unknown";
        info_static.FullChargedCapacity = "Unknown";
        info_static.DefaultAlert1 = "Unknown";
        info_static.DefaultAlert2 = "Unknown";
    }

    if (info_static.capacityValid)
    {
        double wear = 100.0 - (bi.FullChargedCapacity * 100.0) / bi.DesignedCapacity;
        if (wear < 0.0)
            wear = 0.0;

        info_static.wearPercent = wear;
        info_static.wearValid = true;

        info_static.WearLevel = std::format("{:.0f}%", wear);
    }
    else
    {
        info_static.WearLevel = "Unknown";
    }

    return true;
}

bool batteryinfo_bi::QueryBatteryStatus()
{
    BATTERY_WAIT_STATUS bws = {};

    if (!TaggedIoctl(IOCTL_BATTERY_QUERY_STATUS,
                     &bws, sizeof(bws), &bs, sizeof(bs)))
        return false;

    info_1s.charging = (bs.PowerState & BATTERY_CHARGING) != 0;
    info_1s.discharging = (bs.PowerState & BATTERY_DISCHARGING) != 0;
    info_1s.onLine = (bs.PowerState & BATTERY_POWER_ON_LINE) != 0;

    info_1s.voltageValid = bs.Voltage != BATTERY_UNKNOWN_VOLTAGE;
    if (info_1s.voltageValid)
    {
        info_1s.voltageV = bs.Voltage / 1000.0;
        info_1s.Voltage = std::format("{:.2f} V", info_1s.voltageV);
    }
    else
    {
        info_1s.Voltage = "Unknown";
    }

    info_1s.rateValid = (LONG)bs.Rate != (LONG)BATTERY_UNKNOWN_RATE;
    if (info_1s.rateValid)
    {
        info_1s.rateW = (double)(LONG)bs.Rate / 1000.0;
        info_1s.Rate = std::format("{:.2f} W", info_1s.rateW);
    }
    else
    {
        info_1s.Rate = "Unknown";
    }

    info_1s.PowerState =
        info_1s.charging ? "Charging" : info_1s.discharging ? "Discharging"
                                                            : "Idle";

    info_1s.remainingValid = bs.Capacity != BATTERY_UNKNOWN_CAPACITY && info_static.capacityValid;
    if (info_1s.remainingValid)
    {
        info_1s.remainingWh = bs.Capacity / 1000.0;
        info_1s.RemainingCapacity = std::format("{:.2f} Wh", info_1s.remainingWh);
    }
    else
    {
        info_1s.RemainingCapacity = "Unknown";
    }

    info_1s.chargeValid = bs.Capacity != BATTERY_UNKNOWN_CAPACITY && bi.FullChargedCapacity > 0;
    if (info_1s.chargeValid)
    {
        info_1s.chargePercent = (bs.Capacity * 100.0) / bi.FullChargedCapacity;
        if (info_1s.chargePercent > 100.0)
            info_1s.chargePercent = 100.0;

        info_1s.ChargeLevel = std::format("{:.0f}%", info_1s.chargePercent);
    }
    else
    {
        info_1s.ChargeLevel = "Unknown";
    }

    return true;
}

bool batteryinfo_bi::QueryBatteryRemaining()
{
    info_10s.minutesToEmpty = -1;
    info_10s.minutesToFull = -1;

    if (bi.FullChargedCapacity == 0)
        return true;

    LONGLONG rate = (LONGLONG)(LONG)bs.Rate;
    ULONGLONG rate_mW = (rate < 0) ? (ULONGLONG)(-rate) : (ULONGLONG)rate;
    bool rateUsable = info_1s.rateValid && rate_mW > 0;

    if (info_1s.discharging && rateUsable)
    {
        ULONGLONG minutes = ((ULONGLONG)bs.Capacity * 60) / rate_mW;
        info_10s.minutesToEmpty =
            minutes <= (ULONGLONG)INT_MAX ? (int)minutes : INT_MAX;
        info_10s.TimeRemaining = std::format("{}h. {}m.",
                                             info_10s.minutesToEmpty / 60,
                                             info_10s.minutesToEmpty % 60);
    }
    else if (info_1s.discharging)
    {
        info_10s.TimeRemaining = "Calculating...";
    }
    else
    {
        info_10s.TimeRemaining = "Not discharging";
    }

    if (info_1s.charging && rateUsable)
    {
        ULONG missing = (bi.FullChargedCapacity > bs.Capacity)
                            ? (bi.FullChargedCapacity - bs.Capacity)
                            : 0;

        ULONGLONG minutes = ((ULONGLONG)missing * 60) / rate_mW;
        info_10s.minutesToFull =
            minutes <= (ULONGLONG)INT_MAX ? (int)minutes : INT_MAX;
        info_10s.TimeToFullCharge = std::format("{}h. {}m.",
                                                info_10s.minutesToFull / 60,
                                                info_10s.minutesToFull % 60);
    }
    else if (info_1s.charging)
    {
        info_10s.TimeToFullCharge = "Calculating...";
    }
    else
    {
        info_10s.TimeToFullCharge = "Not charging";
    }

    return true;
}

void batteryinfo_bi::PrintAllConsole() const
{
}
