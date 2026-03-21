#pragma once

#include "randomizerTypes.h"

#include <string>
#include <vector>

namespace CheckTracker {

enum class CheckAgeRequirement { Any, ChildOnly, AdultOnly };
enum class CheckTimeRequirement { Any, DayOnly, NightOnly };

struct CheckAgeTimeAvailabilityInfo {
    bool canChildDay = false;
    bool canChildNight = false;
    bool canAdultDay = false;
    bool canAdultNight = false;
    bool canDoNow = false;
    bool canDoAtAll = false;
    CheckAgeRequirement ageRequirement = CheckAgeRequirement::Any;
    CheckTimeRequirement timeRequirement = CheckTimeRequirement::Any;
};

std::vector<std::string> GetCheckLogicBranches(RandomizerCheck rc);
std::string GetCheckLogicString(RandomizerCheck rc);
CheckAgeTimeAvailabilityInfo EvaluateCheckAgeTimeAvailability(RandomizerCheck rc);
bool IsCheckAvailableButWrongAgeOrTime(RandomizerCheck rc);
std::string GetCheckRequirementSummary(RandomizerCheck rc);

} // namespace CheckTracker
