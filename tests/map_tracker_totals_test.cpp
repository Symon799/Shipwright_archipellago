#include "check.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_totals.h"

int main() {
    CheckTracker::MapCheckTotals totals;
    CHECK(totals.total == 0 && totals.checked == 0 && totals.accessible == 0);
    totals.AddVisibleCheck(false, true);  // Green.
    totals.AddVisibleCheck(false, true);  // Orange: reachable at another age/time.
    totals.AddVisibleCheck(false, false); // Unreachable.
    totals.AddVisibleCheck(true, false);
    totals.AddVisibleCheck(true, true); // Collected takes precedence over accessibility.
    CHECK(totals.total == 5 && totals.checked == 2 && totals.accessible == 2);
    totals = {};
    for (int i = 0; i < 2000; ++i) {
        totals.AddVisibleCheck(i % 4 == 0, i % 2 == 0);
    }
    CHECK(totals.total == 2000 && totals.checked == 500 && totals.accessible == 500);
}
