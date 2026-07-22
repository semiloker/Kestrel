#include "BatteryInfo.h"

bool batteryinfo_bi::Initialize()
{
    hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_BATTERY, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hDevInfo == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVICE_INTERFACE_DATA did =
        {
            sizeof(SP_DEVICE_INTERFACE_DATA)};

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

    if (hBattery == INVALID_HANDLE_VALUE)
        return false;

    return QueryTag() && QueryBatteryInfo() && QueryBatteryStatus() && QueryBatteryRemaining()  && QueryBatteryCycleCount();
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

    if (FAILED(hres))
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
    BSTR query = SysAllocString(L"SELECT * FROM BatteryStatus");
    hres = pSvc->ExecQuery(
        language, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnumerator);
    SysFreeString(language);
    SysFreeString(query);

    if (FAILED(hres))
    {
        info_static.CycleCount = "Query failed";
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject *pClassObject = nullptr;
    ULONG uReturn = 0;

    if (pEnumerator->Next(WBEM_INFINITE, 1, &pClassObject, &uReturn) == S_OK)
    {
        VARIANT vtProp;
        VariantInit(&vtProp);

        hres = pClassObject->Get(L"CycleCount", 0, &vtProp, 0, 0);

        if (SUCCEEDED(hres) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4))
        {
            std::ostringstream oss;
            oss << vtProp.uintVal << " cycles";
            info_static.CycleCount = oss.str();
        }
        else
        {
            info_static.CycleCount = "Unsupported";
        }

        VariantClear(&vtProp);
        pClassObject->Release();
    }
    else
    {
        info_static.CycleCount = "No data";
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
    bqi.BatteryTag = tag;
    bqi.InformationLevel = BatteryInformation;

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_INFORMATION,
                         &bqi, sizeof(bqi), &bi, sizeof(bi), &bytesReturned, NULL))
        return false;

    info_static.Chemistry = std::string((char *)bi.Chemistry, 4);
    info_static.DesignedCapacity = std::to_string(bi.DesignedCapacity) + " mWh (" +
                                   std::to_string(bi.DesignedCapacity / 1000.0) + " mW)";
    info_static.FullChargedCapacity = std::to_string(bi.FullChargedCapacity) + " mWh (" +
                                      std::to_string(bi.FullChargedCapacity / 1000.0) + " mW)";
    info_static.DefaultAlert1 = std::to_string(bi.DefaultAlert1) + " mWh (" +
                                std::to_string(bi.DefaultAlert1 / 1000.0) + " mW)";
    info_static.DefaultAlert2 = std::to_string(bi.DefaultAlert2) + " mWh (" +
                                std::to_string(bi.DefaultAlert2 / 1000.0) + " mW)";

    if (bi.DesignedCapacity > 0)
    {
        int wear = 100 - (bi.FullChargedCapacity * 100 / bi.DesignedCapacity);
        info_static.WearLevel = std::to_string(wear) + "%";
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
    bws.BatteryTag = tag;

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_STATUS,
                         &bws, sizeof(bws), &bs, sizeof(bs), &bytesReturned, NULL))
        return false;

    char buf[64];

    snprintf(buf, sizeof(buf), "%.2f V", bs.Voltage / 1000.0);
    info_1s.Voltage = buf;

    snprintf(buf, sizeof(buf), "%.2f W", (double)(LONG)bs.Rate / 1000.0);
    info_1s.Rate = buf;

    info_1s.PowerState =
        (bs.PowerState & BATTERY_CHARGING) ? "Charging" : (bs.PowerState & BATTERY_DISCHARGING) ? "Discharging"
                                                                                                : "Idle";

    snprintf(buf, sizeof(buf), "%.2f Wh", bs.Capacity / 1000.0);
    info_1s.RemainingCapacity = buf;

    if (bi.FullChargedCapacity > 0)
    {
        snprintf(buf, sizeof(buf), "%.2f%%", (bs.Capacity * 100.0) / bi.FullChargedCapacity);
        info_1s.ChargeLevel = buf;
    }

    return true;
}

bool batteryinfo_bi::QueryBatteryRemaining()
{
    if (bi.FullChargedCapacity > 0)
    {
        char buf[64];

        if ((bs.PowerState & BATTERY_DISCHARGING) && bs.Rate != 0)
        {
            int rate_mW = abs(bs.Rate);
            if (rate_mW > 0)
            {
                int remainingMinutes = (bs.Capacity * 60) / rate_mW;
                snprintf(buf, sizeof(buf), "%dh. %dm.",
                         remainingMinutes / 60, remainingMinutes % 60);
                info_10s.TimeRemaining = buf;
            }
            else
            {
                info_10s.TimeRemaining = "Calculating...";
            }
        }
        else
        {
            info_10s.TimeRemaining = "Not discharging";
        }

        if ((bs.PowerState & BATTERY_CHARGING) && bs.Rate != 0)
        {
            int rate_mW = abs(bs.Rate);
            int remainingCapacity = bi.FullChargedCapacity - bs.Capacity;
            if (rate_mW > 0)
            {
                int timeToFullMinutes = (remainingCapacity * 60) / rate_mW;
                snprintf(buf, sizeof(buf), "%dh. %dm.",
                         timeToFullMinutes / 60, timeToFullMinutes % 60);
                info_10s.TimeToFullCharge = buf;
            }
            else
            {
                info_10s.TimeToFullCharge = "Calculating...";
            }
        }
        else
        {
            info_10s.TimeToFullCharge = "Not Charging";
        }
    }

    return true;
}

void batteryinfo_bi::PrintAllConsole() const
{
}
