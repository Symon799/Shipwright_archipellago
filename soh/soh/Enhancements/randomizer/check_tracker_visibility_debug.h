#pragma once

#include "randomizerTypes.h"

#include <string>

namespace CheckTracker {

struct CheckVisibilityDiagnostic {
    RandomizerCheck check = RC_UNKNOWN_CHECK;
    std::string mapTrackerId;
    std::string displayName;
    std::string rcType;
    std::string quest;
    std::string vanillaItem;
    std::string placedItem;
    bool inChecksByArea = false;
    bool isExcluded = false;
    bool questActive = false;
    bool showKeysanityFlag = false;
    bool smallKeyTypeGatePass = false;
    bool checkShuffled = false;
    bool visibleInTracker = false;
    bool mapPackLinked = false;
    bool mapPackUnassigned = false;
    bool areaSpoiled = false;
    std::string primaryBlockReason;
};

CheckVisibilityDiagnostic BuildCheckVisibilityDiagnostic(RandomizerCheck rc);

// Writes <app>/logs/check_tracker_visibility_debug.txt with soh_id lists for map-pack and tracker-visible checks.
// Returns an empty string on failure.
std::string WriteCheckTrackerVisibilityDebugLog();

void DrawCheckTrackerVisibilityDebugControls();

} // namespace CheckTracker
