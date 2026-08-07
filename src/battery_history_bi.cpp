#include "battery_history_bi.h"

#include "BatteryInfo.h"
#include "logger_bi.h"
#include "paths_bi.h"

#include <cstdio>
#include <format>
#include <string>

namespace
{
    // Roughly a decade of five-minute rows. Past this the file stops growing
    // rather than filling the user's profile in the background.
    const LONGLONG MAX_HISTORY_BYTES = 4LL * 1024LL * 1024LL;

    // An unmeasured field is left empty rather than written as 0. index.csv
    // stores 0.0 for "never measured" and cannot distinguish a flat battery
    // from a missing sensor; this file does not repeat that.
    std::string cell(bool valid, double value, int decimals)
    {
        if (!valid)
            return std::string();
        if (decimals <= 0)
            return std::format("{:.0f}", value);
        if (decimals == 1)
            return std::format("{:.1f}", value);
        return std::format("{:.2f}", value);
    }
}

void battery_history_bi::setIntervalMinutes(int minutes)
{
    if (minutes < 1)
        minutes = 1;
    if (minutes > 1440)
        minutes = 1440;
    intervalMinutes = minutes;
}

void battery_history_bi::tick(const batteryinfo_bi *bi)
{
    if (!enabled || !bi || !bi->present)
        return;

    ULONGLONG now = GetTickCount64();
    ULONGLONG intervalMs = (ULONGLONG)intervalMinutes * 60ULL * 1000ULL;

    // The first row lands straight away, so a machine that is only awake for a
    // few minutes still records where the battery stood.
    if (wroteOnce && now - lastWriteTick < intervalMs)
        return;

    lastWriteTick = now;
    wroteOnce = true;
    writeRow(bi);
}

bool battery_history_bi::writeRow(const batteryinfo_bi *bi)
{
    const std::wstring path = paths_bi::inDataDirWide(L"battery-history.csv");

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
    {
        LONGLONG size = ((LONGLONG)attributes.nFileSizeHigh << 32) |
                        (LONGLONG)attributes.nFileSizeLow;
        if (size >= MAX_HISTORY_BYTES)
        {
            if (!cappedLogged)
            {
                log_bi::write("battery history: battery-history.csv is at the %lld byte cap, "
                              "no longer appending",
                              (long long)MAX_HISTORY_BYTES);
                cappedLogged = true;
            }
            return false;
        }
    }

    bool fresh = false;
    FILE *file = paths_bi::openRegularAppend(path, &fresh);
    if (!file)
    {
        if (!openFailedLogged)
        {
            log_bi::write("battery history: could not open battery-history.csv for append");
            openFailedLogged = true;
        }
        return false;
    }

    if (fresh)
    {
        fputs("timestamp,charge_pct,remaining_wh,full_wh,design_wh,wear_pct,"
              "cycles,temp_c,rate_w,on_line,charging\n", file);
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);

    std::string row = std::format(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02},{},{},{},{},{},{},{},{},{},{}\n",
        (int)now.wYear, (int)now.wMonth, (int)now.wDay,
        (int)now.wHour, (int)now.wMinute, (int)now.wSecond,
        cell(bi->info_1s.chargeValid, bi->info_1s.chargePercent, 1),
        cell(bi->info_1s.remainingValid, bi->info_1s.remainingWh, 2),
        cell(bi->info_static.capacityValid, bi->info_static.fullChargedWh, 2),
        cell(bi->info_static.capacityValid, bi->info_static.designedWh, 2),
        cell(bi->info_static.wearValid, bi->info_static.wearPercent, 1),
        bi->info_static.cycleCountValid
            ? std::to_string(bi->info_static.cycleCount) : std::string(),
        cell(bi->info_1s.tempValid, bi->info_1s.tempC, 1),
        cell(bi->info_1s.rateValid, bi->info_1s.rateW, 2),
        bi->info_1s.onLine ? "1" : "0",
        bi->info_1s.charging ? "1" : "0");

    fputs(row.c_str(), file);
    fclose(file);
    return true;
}
