#include "randomizer_check_logic.h"

#include "location_access.h"

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
#include "variables.h"
#include "macros.h"
}

namespace CheckTracker {
namespace {

struct CheckLocationBranch {
    RandomizerRegion parentRegion = RR_NONE;
    const LocationAccess* locationAccess = nullptr;
};

struct CheckLocationBranchCache {
    size_t totalLocationCount = 0;
    std::array<std::vector<CheckLocationBranch>, static_cast<size_t>(RC_MAX)> branchesByCheck;
};

static size_t ComputeTotalLocationAccessCount() {
    size_t totalLocationCount = 0;
    for (uint32_t regionIndex = RR_ROOT; regionIndex < RR_MAX; regionIndex++) {
        totalLocationCount += areaTable[regionIndex].locations.size();
    }
    return totalLocationCount;
}

static CheckLocationBranchCache BuildCheckLocationBranchCache() {
    CheckLocationBranchCache cache;
    cache.totalLocationCount = ComputeTotalLocationAccessCount();

    for (uint32_t regionIndex = RR_ROOT; regionIndex < RR_MAX; regionIndex++) {
        const RandomizerRegion parentRegion = static_cast<RandomizerRegion>(regionIndex);
        for (const auto& locationAccess : areaTable[regionIndex].locations) {
            const RandomizerCheck location = locationAccess.GetLocation();
            if (location <= RC_UNKNOWN_CHECK || location >= RC_MAX) {
                continue;
            }

            cache.branchesByCheck[static_cast<size_t>(location)].push_back({ parentRegion, &locationAccess });
        }
    }

    return cache;
}

static const std::vector<CheckLocationBranch>& GetCheckLocationBranches(RandomizerCheck rc) {
    static CheckLocationBranchCache cache;
    static const std::vector<CheckLocationBranch> emptyBranches;

    const size_t totalLocationCount = ComputeTotalLocationAccessCount();
    if (cache.totalLocationCount != totalLocationCount) {
        cache = BuildCheckLocationBranchCache();
    }

    if (rc <= RC_UNKNOWN_CHECK || rc >= RC_MAX) {
        return emptyBranches;
    }

    return cache.branchesByCheck[static_cast<size_t>(rc)];
}

static bool EvaluateLocationConditionAtAgeTime(const LocationAccess& locationAccess, RandomizerRegion parentRegion,
                                               RandomizerCheck rc, bool evaluateAsAdult, bool evaluateAtNight) {
    auto ctx = Rando::Context::GetInstance();
    if (ctx == nullptr) {
        return false;
    }

    auto logicRef = ctx->GetLogic();
    if (logicRef == nullptr) {
        return false;
    }

    const bool previousIsChild = logicRef->IsChild;
    const bool previousIsAdult = logicRef->IsAdult;
    const bool previousAtDay = logicRef->AtDay;
    const bool previousAtNight = logicRef->AtNight;
    const RandomizerRegion previousRegionKey = logicRef->CurrentRegionKey;
    const RandomizerCheck previousCheckKey = logicRef->CurrentCheckKey;

    logicRef->CurrentRegionKey = parentRegion;
    logicRef->CurrentCheckKey = rc;

    bool conditionsMet = false;
    if (evaluateAsAdult) {
        conditionsMet = evaluateAtNight ? locationAccess.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtNight)
                                        : locationAccess.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtDay);
    } else {
        conditionsMet = evaluateAtNight ? locationAccess.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtNight)
                                        : locationAccess.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtDay);
    }

    logicRef->IsChild = previousIsChild;
    logicRef->IsAdult = previousIsAdult;
    logicRef->AtDay = previousAtDay;
    logicRef->AtNight = previousAtNight;
    logicRef->CurrentRegionKey = previousRegionKey;
    logicRef->CurrentCheckKey = previousCheckKey;

    return conditionsMet;
}

static CheckAgeTimeAvailabilityInfo BuildAgeTimeAvailabilityInfo(bool canChildDay, bool canChildNight, bool canAdultDay,
                                                                 bool canAdultNight) {
    CheckAgeTimeAvailabilityInfo info;
    info.canChildDay = canChildDay;
    info.canChildNight = canChildNight;
    info.canAdultDay = canAdultDay;
    info.canAdultNight = canAdultNight;
    info.canDoAtAll = canChildDay || canChildNight || canAdultDay || canAdultNight;

    const bool canAsChild = canChildDay || canChildNight;
    const bool canAsAdult = canAdultDay || canAdultNight;
    if (canAsChild != canAsAdult) {
        info.ageRequirement = canAsChild ? CheckAgeRequirement::ChildOnly : CheckAgeRequirement::AdultOnly;
    }

    const bool canAtDay = canChildDay || canAdultDay;
    const bool canAtNight = canChildNight || canAdultNight;
    if (canAtDay != canAtNight) {
        info.timeRequirement = canAtDay ? CheckTimeRequirement::DayOnly : CheckTimeRequirement::NightOnly;
    }

    if (LINK_IS_ADULT) {
        info.canDoNow = IS_NIGHT ? canAdultNight : canAdultDay;
    } else {
        info.canDoNow = IS_NIGHT ? canChildNight : canChildDay;
    }

    return info;
}

static uint8_t BuildAgeTimeAvailabilityMask(const CheckAgeTimeAvailabilityInfo& availabilityInfo) {
    uint8_t availabilityMask = 0;
    availabilityMask |= availabilityInfo.canChildDay ? 0x1 : 0x0;
    availabilityMask |= availabilityInfo.canChildNight ? 0x2 : 0x0;
    availabilityMask |= availabilityInfo.canAdultDay ? 0x4 : 0x0;
    availabilityMask |= availabilityInfo.canAdultNight ? 0x8 : 0x0;
    return availabilityMask;
}

static std::string DescribeAvailabilityMask(uint8_t availabilityMask) {
    switch (availabilityMask) {
        case 0x0:
        case 0xF:
            return "";
        case 0x1:
            return "Child, Day";
        case 0x2:
            return "Child, Night";
        case 0x3:
            return "Child";
        case 0x4:
            return "Adult, Day";
        case 0x5:
            return "Day";
        case 0x6:
            return "Child, Night or Adult, Day";
        case 0x7:
            return "Child or Day";
        case 0x8:
            return "Adult, Night";
        case 0x9:
            return "Child, Day or Adult, Night";
        case 0xA:
            return "Night";
        case 0xB:
            return "Child or Night";
        case 0xC:
            return "Adult";
        case 0xD:
            return "Adult or Day";
        case 0xE:
            return "Adult or Night";
        default:
            return "";
    }
}

static std::string BuildCheckRequirementSummary(const CheckAgeTimeAvailabilityInfo& availabilityInfo) {
    std::string explicitRequirementSummary = DescribeAvailabilityMask(BuildAgeTimeAvailabilityMask(availabilityInfo));
    if (!explicitRequirementSummary.empty()) {
        return "Required: " + explicitRequirementSummary;
    }

    if (availabilityInfo.canDoAtAll && !availabilityInfo.canDoNow) {
        return "Required: Different age/time";
    }

    return "";
}

} // namespace

std::vector<std::string> GetCheckLogicBranches(RandomizerCheck rc) {
    std::vector<std::string> logicBranches;
    const auto& branches = GetCheckLocationBranches(rc);
    logicBranches.reserve(branches.size());

    std::unordered_set<std::string> seenConditions;
    seenConditions.reserve(branches.size());

    for (const auto& branch : branches) {
        if (branch.locationAccess == nullptr) {
            continue;
        }

        const std::string conditionString = branch.locationAccess->GetConditionStr();
        if (conditionString.empty() || conditionString == "true") {
            continue;
        }

        if (seenConditions.insert(conditionString).second) {
            logicBranches.push_back(conditionString);
        }
    }

    return logicBranches;
}

std::string GetCheckLogicString(RandomizerCheck rc) {
    const auto logicBranches = GetCheckLogicBranches(rc);
    if (logicBranches.empty()) {
        return "";
    }

    std::string combinedLogicString;
    for (size_t branchIndex = 0; branchIndex < logicBranches.size(); branchIndex++) {
        if (branchIndex > 0) {
            combinedLogicString += "\n\n----- OR -----\n\n";
        }
        combinedLogicString += logicBranches[branchIndex];
    }

    return combinedLogicString;
}

CheckAgeTimeAvailabilityInfo EvaluateCheckAgeTimeAvailability(RandomizerCheck rc) {
    bool canChildDay = false;
    bool canChildNight = false;
    bool canAdultDay = false;
    bool canAdultNight = false;

    for (const auto& branch : GetCheckLocationBranches(rc)) {
        if (branch.locationAccess == nullptr || branch.parentRegion == RR_NONE || branch.parentRegion >= RR_MAX) {
            continue;
        }

        Region& parentRegion = areaTable[branch.parentRegion];
        canChildDay |= parentRegion.childDay &&
                       EvaluateLocationConditionAtAgeTime(*branch.locationAccess, branch.parentRegion, rc, false, false);
        canChildNight |= parentRegion.childNight &&
                         EvaluateLocationConditionAtAgeTime(*branch.locationAccess, branch.parentRegion, rc, false, true);
        canAdultDay |= parentRegion.adultDay &&
                       EvaluateLocationConditionAtAgeTime(*branch.locationAccess, branch.parentRegion, rc, true, false);
        canAdultNight |= parentRegion.adultNight &&
                         EvaluateLocationConditionAtAgeTime(*branch.locationAccess, branch.parentRegion, rc, true, true);
    }

    return BuildAgeTimeAvailabilityInfo(canChildDay, canChildNight, canAdultDay, canAdultNight);
}

bool IsCheckAvailableButWrongAgeOrTime(RandomizerCheck rc) {
    CheckAgeTimeAvailabilityInfo availabilityInfo = EvaluateCheckAgeTimeAvailability(rc);
    return availabilityInfo.canDoAtAll && !availabilityInfo.canDoNow;
}

std::string GetCheckRequirementSummary(RandomizerCheck rc) {
    return BuildCheckRequirementSummary(EvaluateCheckAgeTimeAvailability(rc));
}

} // namespace CheckTracker
