#include "check.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_display_mode.h"

int main() {
    using namespace MapTracker;
    CHECK(DEFAULT_DISPLAY_MODE == 0);
    for (int mode = 0; mode < 4; ++mode) {
        for (bool inGame : { false, true }) {
            for (bool native : { false, true }) {
                for (bool floating : { false, true }) {
                    CHECK(ResolveDisplayMode(mode, inGame, native, floating) == static_cast<DisplayMode>(mode));
                }
            }
        }
    }
    CHECK(ResolveDisplayMode(-1, true, true, false) == DisplayMode::Pause3D);
    CHECK(ResolveDisplayMode(-1, true, false, true) == DisplayMode::Pause2D);
    CHECK(ResolveDisplayMode(-1, false, true, false) == DisplayMode::Windowed);
    CHECK(ResolveDisplayMode(-1, false, false, true) == DisplayMode::Floating);
    CHECK(ResolveDisplayMode(99, true, true, false) == DisplayMode::Floating);
    CHECK(ResolveDisplayMode(-99, true, false, false) == DisplayMode::Pause2D);
}
