#pragma once

#include <unordered_set>
#include <string>
#include <vector>

namespace CheckTracker {
enum class ArchipelagoVisibility { Hidden, Visible, SeedFallback };

inline ArchipelagoVisibility ResolveArchipelagoVisibility(bool excluded, bool scouted, bool scoutsReady) {
    if (excluded) {
        return ArchipelagoVisibility::Hidden;
    }
    if (scouted) {
        return ArchipelagoVisibility::Visible;
    }
    return scoutsReady ? ArchipelagoVisibility::Hidden : ArchipelagoVisibility::SeedFallback;
}

template <class Check> struct ScoutedCheckIndex {
    std::unordered_set<Check> checks;
    std::vector<std::string> unresolvedNames;

    template <class Items, class Resolver> void Update(const Items& items, Resolver resolve) {
        checks.clear();
        unresolvedNames.clear();
        for (const auto& item : items) {
            auto check = resolve(item.locationName);
            if (check) {
                checks.insert(*check);
            } else {
                unresolvedNames.push_back(item.locationName);
            }
        }
    }
};
} // namespace CheckTracker
