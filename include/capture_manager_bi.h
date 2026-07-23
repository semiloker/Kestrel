#ifndef CAPTURE_MANAGER_BI_H
#define CAPTURE_MANAGER_BI_H

#include <string>
#include <vector>

#include "capture_bi.h"

class capture_manager_bi
{
public:
    capture_manager_bi();
    ~capture_manager_bi() = default;

    void toggle(const std::string &processName);
    bool active() const { return cap_.active(); }

    void stopForRollback();
    void loadHistory();

    capture_bi &getCapture() { return cap_; }
    const capture_bi &getCapture() const { return cap_; }

    const capture_bi::summary_bi &lastSummary() const { return lastCapture; }
    bool hasLastSummary() const { return haveLastCapture; }

    const std::vector<capture_bi::summary_bi> &history() const { return captureHistory; }
    std::vector<capture_bi::summary_bi> &history() { return captureHistory; }

    bool historyLoaded() const { return captureHistoryLoaded; }

private:
    capture_bi cap_;
    capture_bi::summary_bi lastCapture;
    bool haveLastCapture = false;
    std::vector<capture_bi::summary_bi> captureHistory;
    bool captureHistoryLoaded = false;
};

#endif
