#pragma once

#include <cstdint>

namespace CheckTracker {
struct MapCheckTotals {
    uint16_t checked = 0;
    uint16_t accessible = 0;
    uint16_t total = 0;

    void AddVisibleCheck(bool done, bool reachable) {
        ++total;
        if (done) {
            ++checked;
        } else if (reachable) {
            ++accessible;
        }
    }
};
} // namespace CheckTracker
