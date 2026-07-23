#include "capture_manager_bi.h"

capture_manager_bi::capture_manager_bi()
{
}

void capture_manager_bi::toggle(const std::string &processName)
{
    if (cap_.active())
    {
        capture_bi::summary_bi summary;
        if (cap_.stop(&summary))
        {
            lastCapture = summary;
            haveLastCapture = true;
            captureHistoryLoaded = false;
        }
    }
    else
    {
        cap_.start(processName);
    }
}

void capture_manager_bi::stopForRollback()
{
    if (cap_.active())
    {
        capture_bi::summary_bi s;
        cap_.stop(&s);
        lastCapture = s;
        haveLastCapture = true;
        captureHistoryLoaded = false;
    }
}

void capture_manager_bi::loadHistory()
{
    if (!captureHistoryLoaded)
    {
        capture_bi::loadIndex(&captureHistory);
        captureHistoryLoaded = true;
    }
}
