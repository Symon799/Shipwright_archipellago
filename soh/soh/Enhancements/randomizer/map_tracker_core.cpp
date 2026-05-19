#include "map_tracker_internal.h"
#include "map_tracker_link_resolver.h"
#include "randomizer_entrance_tracker.h"
#include "randomizer_item_tracker.h"
#include "randomizerTypes.h"
#include "soh/OTRGlobals.h"
#include "soh/SaveManager.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/util.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "dungeon.h"
#include "entrance.h"
#include "location_access.h"
#include "3drando/fill.hpp"
#include "soh/Enhancements/debugger/performanceTimer.h"

extern "C" {
#include "variables.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <libultraship/libultraship.h>
#include "location.h"
#include "item_location.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace CheckTracker {
using json = nlohmann::json;
using namespace UIWidgets;

MapTrackerState mapTrackerState;
static uint64_t mapTrackerTextureLoadGeneration = 1;

std::string TrimCopy(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string GetGameCheckMapTrackerId(RandomizerCheck rc) {
    auto* location = Rando::StaticData::GetLocation(rc);
    if (location == nullptr) {
        return "";
    }

    return std::string(location->GetMapTrackerId());
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    if (value.length() < suffix.length()) {
        return false;
    }
    return value.compare(value.length() - suffix.length(), suffix.length(), suffix) == 0;
}

std::string JoinWithComma(const std::vector<std::string>& values) {
    std::string output;
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) {
            output += ", ";
        }
        output += values[i];
    }
    return output;
}

std::string JoinWithCommaLimited(const std::vector<std::string>& values, size_t maxValues) {
    if (values.empty() || maxValues == 0) {
        return "";
    }

    size_t shownCount = std::min(values.size(), maxValues);
    std::vector<std::string> shownValues(values.begin(), values.begin() + shownCount);
    std::string joined = JoinWithComma(shownValues);
    if (values.size() > shownCount) {
        joined += fmt::format(" (+{} more)", values.size() - shownCount);
    }
    return joined;
}

double GetElapsedMilliseconds(const std::chrono::steady_clock::time_point& startTime) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
}

bool TryReadFloat(const json& value, float& outValue) {
    if (value.is_number_float()) {
        outValue = value.get<float>();
        return true;
    }
    if (value.is_number_integer()) {
        outValue = static_cast<float>(value.get<int>());
        return true;
    }
    if (value.is_string()) {
        std::string str = TrimCopy(value.get<std::string>());
        if (str.empty()) {
            return false;
        }
        try {
            outValue = std::stof(str);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::vector<std::filesystem::path> BuildMapTrackerAssetsRootCandidates() {
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(Ship::Context::GetPathRelativeToAppDirectory(CHECK_TRACKER_MAP_ASSETS_ROOT));
    candidates.emplace_back(Ship::Context::GetPathRelativeToAppBundle(CHECK_TRACKER_MAP_ASSETS_ROOT));
    candidates.emplace_back(std::filesystem::path(CHECK_TRACKER_MAP_ASSETS_ROOT));

    std::error_code ec;
    const std::filesystem::path currentPath = std::filesystem::current_path(ec);
    if (!ec) {
        candidates.emplace_back((currentPath / CHECK_TRACKER_MAP_ASSETS_ROOT).lexically_normal());
        candidates.emplace_back((currentPath / "build/soh" / CHECK_TRACKER_MAP_ASSETS_ROOT).lexically_normal());
        candidates.emplace_back((currentPath.parent_path() / "build/soh" / CHECK_TRACKER_MAP_ASSETS_ROOT).lexically_normal());
    }

    std::vector<std::filesystem::path> uniqueCandidates;
    std::unordered_set<std::string> seenPaths;
    uniqueCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const std::filesystem::path normalized = candidate.lexically_normal();
        if (normalized.empty()) {
            continue;
        }
        const std::string key = normalized.generic_string();
        if (seenPaths.insert(key).second) {
            uniqueCandidates.push_back(normalized);
        }
    }

    return uniqueCandidates;
}

std::string BuildMapTrackerAssetsRootCandidatesSummary() {
    std::vector<std::string> candidateStrings;
    const auto candidates = BuildMapTrackerAssetsRootCandidates();
    candidateStrings.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        std::error_code ec;
        const auto absolutePath = std::filesystem::absolute(candidate, ec);
        candidateStrings.push_back(ec ? candidate.string() : absolutePath.string());
    }
    return JoinWithCommaLimited(candidateStrings, candidateStrings.size());
}

std::filesystem::path GetMapTrackerAssetsRoot() {
    std::error_code ec;
    for (const auto& candidate : BuildMapTrackerAssetsRootCandidates()) {
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            return candidate;
        }
        ec.clear();
    }

    const auto fallbackCandidates = BuildMapTrackerAssetsRootCandidates();
    if (!fallbackCandidates.empty()) {
        return fallbackCandidates.front();
    }

    return std::filesystem::path(CHECK_TRACKER_MAP_ASSETS_ROOT);
}

std::vector<std::filesystem::path> FindMapPackZipFiles(const std::filesystem::path& packFolderPath) {
    std::vector<std::filesystem::path> zipFiles;
    std::error_code ec;
    if (!std::filesystem::exists(packFolderPath, ec) || !std::filesystem::is_directory(packFolderPath, ec)) {
        return zipFiles;
    }

    for (const auto& directoryEntry : std::filesystem::directory_iterator(packFolderPath, ec)) {
        if (ec || !directoryEntry.is_regular_file()) {
            continue;
        }

        if (directoryEntry.path().extension() == ".zip") {
            zipFiles.push_back(directoryEntry.path());
        }
    }

    std::sort(zipFiles.begin(), zipFiles.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
        std::error_code leftEc;
        std::error_code rightEc;
        const auto leftWriteTime = std::filesystem::last_write_time(left, leftEc);
        const auto rightWriteTime = std::filesystem::last_write_time(right, rightEc);

        if (leftEc != rightEc) {
            return !leftEc;
        }

        if (!leftEc && leftWriteTime != rightWriteTime) {
            return leftWriteTime > rightWriteTime;
        }

        return left.filename().string() < right.filename().string();
    });
    return zipFiles;
}

std::filesystem::path GetNewestMapPackZip(const std::filesystem::path& packFolderPath) {
    auto zipFiles = FindMapPackZipFiles(packFolderPath);
    if (zipFiles.empty()) {
        return {};
    }
    return zipFiles.front();
}

std::string GetMapTrackerAssetsRootAbsoluteString() {
    const std::filesystem::path resolvedRoot = GetMapTrackerAssetsRoot();
    std::error_code ec;
    std::filesystem::path absolutePath = std::filesystem::absolute(resolvedRoot, ec);
    if (ec) {
        return resolvedRoot.string();
    }
    return absolutePath.string();
}

static bool IsArchivePathMounted(const std::filesystem::path& archivePath) {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        return false;
    }

    const std::string requestedArchivePath = archivePath.lexically_normal().generic_string();
    auto mountedArchives = context->GetResourceManager()->GetArchiveManager()->GetArchives();
    if (mountedArchives == nullptr) {
        return false;
    }

    for (const auto& archive : *mountedArchives) {
        if (archive == nullptr) {
            continue;
        }

        const std::filesystem::path mountedArchivePath = archive->GetPath();
        if (mountedArchivePath.lexically_normal().generic_string() == requestedArchivePath) {
            return true;
        }
    }

    return false;
}

static void UnloadMapTrackerResources(MapTrackerState& state, bool unloadTextures) {
    auto context = Ship::Context::GetInstance();
    if (unloadTextures && context != nullptr && context->GetWindow() != nullptr) {
        auto gui = context->GetWindow()->GetGui();
        if (gui != nullptr) {
            for (const auto& tab : state.tabs) {
                if (!tab.textureName.empty() && gui->HasTextureByName(tab.textureName)) {
                    gui->UnloadTexture(tab.textureName);
                }
            }
        }
    }

    if (!state.mountedArchivePath.empty() && context != nullptr && context->GetResourceManager() != nullptr &&
        context->GetResourceManager()->GetArchiveManager() != nullptr) {
        context->GetResourceManager()->GetArchiveManager()->RemoveArchive(state.mountedArchivePath.string());
    }
}

static void PreserveMapTrackerSessionState(const MapTrackerState& previousState, MapTrackerState& loadedState) {
    loadedState.revealedCheckHints = previousState.revealedCheckHints;
    loadedState.selectedGroupName = previousState.selectedGroupName;
    loadedState.lastSelectedTabByGroup = previousState.lastSelectedTabByGroup;
    loadedState.lastMapViewTabIndex = previousState.lastMapViewTabIndex;
    loadedState.lastFocusedArea = previousState.lastFocusedArea;
    loadedState.lastFocusedScene = previousState.lastFocusedScene;
    loadedState.requestedTabId = previousState.requestedTabId;

    if (loadedState.requestedTabId.empty() && previousState.selectedTabIndex >= 0 &&
        previousState.selectedTabIndex < static_cast<int>(previousState.tabs.size())) {
        loadedState.requestedTabId = previousState.tabs[static_cast<size_t>(previousState.selectedTabIndex)].mapId;
    }
}

static void AppendReloadFailureWarning(MapTrackerState& stateToKeep, const MapTrackerState& failedState) {
    if (failedState.fatalErrors.empty()) {
        return;
    }

    std::vector<std::string> failureSummary;
    failureSummary.reserve(std::min<size_t>(3, failedState.fatalErrors.size()));
    for (size_t i = 0; i < failedState.fatalErrors.size() && i < 3; i++) {
        failureSummary.push_back(failedState.fatalErrors[i]);
    }

    stateToKeep.warnings.insert(stateToKeep.warnings.begin(),
                                { "Reload failed; keeping previous map pack active",
                                  JoinWithCommaLimited(failureSummary, failureSummary.size()) });
}

static std::string BuildMapTextureName(uint64_t loadGeneration, std::string_view mapId) {
    return fmt::format("CHECK_TRACKER_MAP_{}_{}", loadGeneration, mapId);
}

std::string BuildMapTrackerResourcePath(const std::string& resourcePathPrefix, const std::string& relativePath) {
    if (resourcePathPrefix.empty()) {
        return std::filesystem::path(relativePath).lexically_normal().generic_string();
    }
    return (std::filesystem::path(resourcePathPrefix) / relativePath).lexically_normal().generic_string();
}

bool LoadJsonFromArchiveResource(const std::string& resourcePath, json& outJson, std::string& outError) {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        outError = "Resource manager is unavailable.";
        return false;
    }

    auto archiveManager = context->GetResourceManager()->GetArchiveManager();
    auto resourceFile = archiveManager->LoadFile(resourcePath);
    if (resourceFile == nullptr || !resourceFile->IsLoaded || resourceFile->Buffer == nullptr) {
        outError = "Could not load archive file: " + resourcePath;
        return false;
    }

    try {
        outJson = json::parse(resourceFile->Buffer->begin(), resourceFile->Buffer->end(), nullptr, true, true);
    } catch (const std::exception& exception) {
        outError = exception.what();
        return false;
    }
    return true;
}

bool EnsureMapTrackerZipArchiveMounted(const std::filesystem::path& archivePath, const std::string& preferredPrefix,
                                       std::filesystem::path& outArchiveMountRoot, std::string& outResourcePathPrefix,
                                       std::string& outError) {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        outError = "Resource manager is unavailable.";
        return false;
    }

    auto archiveManager = context->GetResourceManager()->GetArchiveManager();
    outArchiveMountRoot = archivePath;

    std::string preferredProbePath = BuildMapTrackerResourcePath(preferredPrefix, CHECK_TRACKER_MAPS_JSON);
    if (!IsArchivePathMounted(archivePath)) {
        auto archive = archiveManager->AddArchive(archivePath.string());
        if (archive == nullptr) {
            outError = "Failed to mount archive file: " + archivePath.string();
            return false;
        }
    }

    if (archiveManager->HasFile(preferredProbePath)) {
        outResourcePathPrefix = preferredPrefix;
        return true;
    }

    if (archiveManager->HasFile(CHECK_TRACKER_MAPS_JSON)) {
        outResourcePathPrefix.clear();
        return true;
    }

    std::string fallbackPrefix;
    std::string preferredListPattern = "*/maps.json";
    auto preferredMatches = archiveManager->ListFiles(preferredListPattern);
    if (preferredMatches != nullptr) {
        for (const auto& resourcePath : *preferredMatches) {
            if (!EndsWith(resourcePath, "/maps.json")) {
                continue;
            }
            fallbackPrefix = resourcePath.substr(0, resourcePath.size() - std::string("/maps.json").size());
            break;
        }
    }

    if (fallbackPrefix.empty()) {
        auto allMatches = archiveManager->ListFiles("*maps.json");
        if (allMatches != nullptr) {
            for (const auto& resourcePath : *allMatches) {
                if (resourcePath == CHECK_TRACKER_MAPS_JSON) {
                    fallbackPrefix.clear();
                    break;
                }
                if (EndsWith(resourcePath, "/maps.json")) {
                    fallbackPrefix = resourcePath.substr(0, resourcePath.size() - std::string("/maps.json").size());
                    break;
                }
            }
        }
    }

    outResourcePathPrefix = fallbackPrefix;
    if (!archiveManager->HasFile(BuildMapTrackerResourcePath(outResourcePathPrefix, CHECK_TRACKER_MAPS_JSON))) {
        outError = "Map pack is not indexed after mount. Missing virtual file: " +
                   BuildMapTrackerResourcePath(preferredPrefix, CHECK_TRACKER_MAPS_JSON);
        return false;
    }

    return true;
}

bool IsMapModeEnabled() {
    return CVarGetInteger(CHECK_TRACKER_MAP_MODE_CVAR, 1) != 0;
}

void SetMapModeEnabled(bool enabled) {
    CVarSetInteger(CHECK_TRACKER_MAP_MODE_CVAR, enabled ? 1 : 0);
}

static void AddSchemaWarning(MapTrackerState& state, std::string context, std::string message) {
    state.warnings.push_back({ context + ": " + message, "" });
}

std::vector<MapPlacement> ExtractPlacementsFromNode(MapTrackerState& state, const json& node, const std::string& sourceFile,
                                                    const std::string& checkLabel, bool& hadSchemaError) {
    std::vector<MapPlacement> placements;
    if (!node.contains("map_locations") || !node["map_locations"].is_array()) {
        return placements;
    }

    for (size_t placementIndex = 0; placementIndex < node["map_locations"].size(); placementIndex++) {
        const auto& mapLoc = node["map_locations"][placementIndex];
        if (!mapLoc.is_object()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid map location in " + sourceFile,
                             fmt::format("Check \"{}\" has a non-object map_locations entry at index {}.", checkLabel,
                                         placementIndex));
            continue;
        }

        if (!mapLoc.contains("map_id") || !mapLoc["map_id"].is_string()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid map location in " + sourceFile,
                             fmt::format("Check \"{}\" is missing a valid string map_id at map_locations[{}].",
                                         checkLabel, placementIndex));
            continue;
        }

        MapPlacement placement;
        placement.mapId = TrimCopy(mapLoc["map_id"].get<std::string>());
        if (placement.mapId.empty()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid map location in " + sourceFile,
                             fmt::format("Check \"{}\" has an empty map_id at map_locations[{}].", checkLabel,
                                         placementIndex));
            continue;
        }

        bool hasX = mapLoc.contains("x") && TryReadFloat(mapLoc["x"], placement.x);
        bool hasY = mapLoc.contains("y") && TryReadFloat(mapLoc["y"], placement.y);
        bool hasSize = true;
        if (mapLoc.contains("size")) {
            hasSize = TryReadFloat(mapLoc["size"], placement.size);
        } else {
            placement.size = 22.0f;
        }

        if (!hasX || !hasY || !hasSize) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid map coordinates in " + sourceFile,
                             fmt::format("Check \"{}\" has invalid x/y/size values at map_locations[{}] for map_id \"{}\".",
                                         checkLabel, placementIndex, placement.mapId));
            continue;
        }

        placements.push_back(placement);
    }

    return placements;
}

std::vector<CheckDescriptor> BuildVisibleCheckDescriptors() {
    std::vector<CheckDescriptor> descriptors;
    std::unordered_set<RandomizerCheck> seenChecks;

    for (auto& [rcArea, checks] : checksByArea) {
        for (auto rc : checks) {
            if (seenChecks.contains(rc) || !IsVisibleInCheckTracker(rc)) {
                continue;
            }
            seenChecks.insert(rc);

            auto* location = Rando::StaticData::GetLocation(rc);
            CheckDescriptor descriptor;
            descriptor.check = rc;
            descriptor.area = location->GetArea();
            descriptor.checkDisplayName = GetCheckDisplayName(rc);
            descriptors.push_back(std::move(descriptor));
        }
    }

    return descriptors;
}

void ResetMapTrackerState(bool unloadTextures) {
    UnloadMapTrackerResources(mapTrackerState, unloadTextures);
    mapTrackerState = {};
    InvalidateMapTrackerRenderCache();
}

static CheckAgeTimeAvailabilityInfo BuildAgeTimeAvailabilityInfo(bool canChildDay, bool canChildNight, bool canAdultDay,
                                                                 bool canAdultNight) {
    CheckAgeTimeAvailabilityInfo info;
    info.canChildDay = canChildDay;
    info.canChildNight = canChildNight;
    info.canAdultDay = canAdultDay;
    info.canAdultNight = canAdultNight;
    info.canDoAtAll = canChildDay || canChildNight || canAdultDay || canAdultNight;

    bool canAsChild = canChildDay || canChildNight;
    bool canAsAdult = canAdultDay || canAdultNight;
    if (canAsChild != canAsAdult) {
        info.ageRequirement = canAsChild ? CheckAgeRequirement::ChildOnly : CheckAgeRequirement::AdultOnly;
    }

    bool canAtDay = canChildDay || canAdultDay;
    bool canAtNight = canChildNight || canAdultNight;
    if (canAtDay != canAtNight) {
        info.timeRequirement = canAtDay ? CheckTimeRequirement::DayOnly : CheckTimeRequirement::NightOnly;
    }

    bool currentIsAdult = LINK_IS_ADULT;
    bool currentIsNight = IS_NIGHT;
    if (currentIsAdult) {
        info.canDoNow = currentIsNight ? canAdultNight : canAdultDay;
    } else {
        info.canDoNow = currentIsNight ? canChildNight : canChildDay;
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

static std::optional<std::string> ResolveFirstExistingMapTabId(
    const std::initializer_list<std::string_view>& preferredMapIds) {
    for (std::string_view preferredMapId : preferredMapIds) {
        const std::string preferredMapIdString(preferredMapId);
        if (mapTrackerState.tabIndexById.contains(preferredMapIdString)) {
            return preferredMapIdString;
        }
    }

    return std::nullopt;
}

std::optional<std::string> ResolvePreferredMapTabIdForArea(RandomizerCheckArea area) {
    switch (area) {
        case RCAREA_KOKIRI_FOREST:
            return ResolveFirstExistingMapTabId({ MapIds::KokiriForest, MapIds::Overworld });
        case RCAREA_LOST_WOODS:
            return ResolveFirstExistingMapTabId({ MapIds::LostWoods, MapIds::Overworld });
        case RCAREA_SACRED_FOREST_MEADOW:
            return ResolveFirstExistingMapTabId({ MapIds::SacredForestMeadow, MapIds::Overworld });
        case RCAREA_HYRULE_FIELD:
            return ResolveFirstExistingMapTabId({ MapIds::HyruleFields, MapIds::Overworld });
        case RCAREA_LAKE_HYLIA:
            return ResolveFirstExistingMapTabId({ MapIds::LakeHylia, MapIds::Overworld });
        case RCAREA_GERUDO_VALLEY:
            return ResolveFirstExistingMapTabId({ MapIds::GerudoValley, MapIds::Overworld });
        case RCAREA_GERUDO_FORTRESS:
            return ResolveFirstExistingMapTabId({ MapIds::GerudoFortress, MapIds::Overworld });
        case RCAREA_WASTELAND:
            return ResolveFirstExistingMapTabId({ MapIds::Wasteland, MapIds::Overworld });
        case RCAREA_DESERT_COLOSSUS:
            return ResolveFirstExistingMapTabId({ MapIds::DesertColossus, MapIds::Overworld });
        case RCAREA_MARKET:
            return ResolveFirstExistingMapTabId({ MapIds::Market, MapIds::Overworld });
        case RCAREA_HYRULE_CASTLE:
            return ResolveFirstExistingMapTabId({ MapIds::HyruleCastle, MapIds::Overworld });
        case RCAREA_KAKARIKO_VILLAGE:
            return ResolveFirstExistingMapTabId({ MapIds::KakarikoVillage, MapIds::Overworld });
        case RCAREA_GRAVEYARD:
            return ResolveFirstExistingMapTabId({ MapIds::Graveyard, MapIds::Overworld });
        case RCAREA_DEATH_MOUNTAIN_TRAIL:
            return ResolveFirstExistingMapTabId({ MapIds::Dmt, MapIds::Overworld });
        case RCAREA_GORON_CITY:
            return ResolveFirstExistingMapTabId({ MapIds::GoronCity, MapIds::Overworld });
        case RCAREA_DEATH_MOUNTAIN_CRATER:
            return ResolveFirstExistingMapTabId({ MapIds::Dmc, MapIds::Overworld });
        case RCAREA_ZORAS_RIVER:
            return ResolveFirstExistingMapTabId({ MapIds::ZoraRiver, MapIds::Overworld });
        case RCAREA_ZORAS_DOMAIN:
            return ResolveFirstExistingMapTabId({ MapIds::ZorasDomain, MapIds::Overworld });
        case RCAREA_ZORAS_FOUNTAIN:
            return ResolveFirstExistingMapTabId({ MapIds::ZorasFountain, MapIds::Overworld });
        case RCAREA_LON_LON_RANCH:
            return ResolveFirstExistingMapTabId({ MapIds::LonLonRanch, MapIds::Overworld });
        case RCAREA_DEKU_TREE:
            return ResolveFirstExistingMapTabId({ MapIds::DekuTree });
        case RCAREA_DODONGOS_CAVERN:
            return ResolveFirstExistingMapTabId({ MapIds::DodongosCavern });
        case RCAREA_JABU_JABUS_BELLY:
            return ResolveFirstExistingMapTabId({ MapIds::JabuJabusBelly });
        case RCAREA_FOREST_TEMPLE:
            return ResolveFirstExistingMapTabId({ MapIds::ForestTemple });
        case RCAREA_FIRE_TEMPLE:
            return ResolveFirstExistingMapTabId({ MapIds::FireTemple });
        case RCAREA_WATER_TEMPLE:
            return ResolveFirstExistingMapTabId({ MapIds::WaterTemple });
        case RCAREA_SPIRIT_TEMPLE:
            return ResolveFirstExistingMapTabId({ MapIds::SpiritTemple });
        case RCAREA_SHADOW_TEMPLE:
            return ResolveFirstExistingMapTabId({ MapIds::ShadowTemple });
        case RCAREA_BOTTOM_OF_THE_WELL:
            return ResolveFirstExistingMapTabId({ MapIds::BottomOfTheWell });
        case RCAREA_ICE_CAVERN:
            return ResolveFirstExistingMapTabId({ MapIds::IceCavern });
        case RCAREA_GERUDO_TRAINING_GROUND:
            return ResolveFirstExistingMapTabId({ MapIds::GerudoTrainingGround });
        case RCAREA_GANONS_CASTLE:
            return ResolveFirstExistingMapTabId({ MapIds::GanonsCastle, MapIds::GanonsTower, MapIds::Overworld });
        default:
            return std::nullopt;
    }
}

static bool EvaluateEntranceConditionAtAgeTime(const Rando::Entrance& entrance, RandomizerRegion parentRegion,
                                               bool evaluateAsAdult, bool evaluateAtNight) {
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

    logicRef->CurrentRegionKey = parentRegion;

    bool conditionsMet = false;
    if (evaluateAsAdult) {
        if (evaluateAtNight) {
            conditionsMet = entrance.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtNight);
        } else {
            conditionsMet = entrance.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtDay);
        }
    } else {
        if (evaluateAtNight) {
            conditionsMet = entrance.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtNight);
        } else {
            conditionsMet = entrance.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtDay);
        }
    }

    logicRef->IsChild = previousIsChild;
    logicRef->IsAdult = previousIsAdult;
    logicRef->AtDay = previousAtDay;
    logicRef->AtNight = previousAtNight;
    logicRef->CurrentRegionKey = previousRegionKey;

    return conditionsMet;
}

std::optional<CheckAgeTimeAvailabilityInfo> EvaluateMapLinkAgeTimeAvailability(const MapLink& link) {
    if (link.disableEntranceLogic || link.entranceIndex < 0) {
        return std::nullopt;
    }

    Rando::Entrance* entrance = Rando::GetEntranceByIndex(link.entranceIndex);
    if (entrance == nullptr) {
        return std::nullopt;
    }

    RandomizerRegion parentRegion = entrance->GetParentRegionKey();
    if (parentRegion == RR_NONE || parentRegion >= RR_MAX) {
        return std::nullopt;
    }

    Region* parent = RegionTable(parentRegion);
    if (parent == nullptr) {
        return std::nullopt;
    }

    auto evaluateCombo = [&](bool parentHasAccess, bool evaluateAsAdult, bool evaluateAtNight) {
        if (!parentHasAccess) {
            return false;
        }
        return EvaluateEntranceConditionAtAgeTime(*entrance, parentRegion, evaluateAsAdult, evaluateAtNight);
    };

    return BuildAgeTimeAvailabilityInfo(evaluateCombo(parent->childDay, false, false),
                                        evaluateCombo(parent->childNight, false, true),
                                        evaluateCombo(parent->adultDay, true, false),
                                        evaluateCombo(parent->adultNight, true, true));
}

std::optional<std::string> GetMapLinkRequirementSummary(const MapLink& link) {
    auto availabilityInfo = EvaluateMapLinkAgeTimeAvailability(link);
    if (!availabilityInfo.has_value()) {
        return std::nullopt;
    }

    std::string summary = BuildCheckRequirementSummary(*availabilityInfo);
    if (summary.empty()) {
        return std::nullopt;
    }
    return summary;
}

MapLinkBorderStyle GetMapLinkBorderStyle(const std::optional<CheckAgeTimeAvailabilityInfo>& availabilityInfo) {
    if (!availabilityInfo.has_value()) {
        return {};
    }

    if (availabilityInfo->canDoNow) {
        return {};
    }

    if (availabilityInfo->canDoAtAll) {
        return { CHECK_TRACKER_MAP_COLOR_LINK_BORDER_AGE_MISMATCH, CHECK_TRACKER_MAP_LINK_UNAVAILABLE_BORDER_THICKNESS };
    }

    return { CHECK_TRACKER_MAP_COLOR_LINK_BORDER_UNAVAILABLE, CHECK_TRACKER_MAP_LINK_UNAVAILABLE_BORDER_THICKNESS };
}

static bool IsGanonsTowerRelatedScene(SceneID scene) {
    switch (scene) {
        case SCENE_GANONS_TOWER:
        case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:
        case SCENE_GANONDORF_BOSS:
        case SCENE_GANON_BOSS:
            return true;
        default:
            return false;
    }
}

static std::optional<std::string> ResolvePreferredMapTabIdForScene(SceneID scene) {
    if (scene == SCENE_TEMPLE_OF_TIME) {
        return ResolveFirstExistingMapTabId({ MapIds::TempleOfTime, MapIds::Market, MapIds::Overworld });
    }

    if (IsGanonsTowerRelatedScene(scene)) {
        return ResolveFirstExistingMapTabId({ MapIds::GanonsTower, MapIds::GanonsCastle, MapIds::Overworld });
    }

    if (scene == SCENE_OUTSIDE_GANONS_CASTLE) {
        if (LINK_IS_ADULT) {
            return ResolveFirstExistingMapTabId({ MapIds::GanonsCastle, MapIds::GanonsTower, MapIds::Overworld });
        }
        return ResolveFirstExistingMapTabId({ MapIds::HyruleCastle, MapIds::Overworld });
    }

    return std::nullopt;
}

void UpdateRequestedMapTabFromCurrentArea(bool force) {
    RandomizerCheckArea focusArea = currentArea;
    SceneID focusScene = SCENE_ID_MAX;

    // Keep auto-focus robust even if transition hooks are delayed/missed for a frame.
    if (gPlayState != nullptr) {
        focusScene = static_cast<SceneID>(gPlayState->sceneNum);
        RandomizerCheckArea liveArea = GetCheckArea();
        if (liveArea != RCAREA_INVALID) {
            if (liveArea != currentArea) {
                previousArea = currentArea;
                currentArea = liveArea;
            }
            focusArea = liveArea;
        }
    }

    if (!force && focusArea == mapTrackerState.lastFocusedArea && focusScene == mapTrackerState.lastFocusedScene) {
        return;
    }
    mapTrackerState.lastFocusedArea = focusArea;
    mapTrackerState.lastFocusedScene = focusScene;

    auto preferredTabId = ResolvePreferredMapTabIdForScene(focusScene);
    if (!preferredTabId.has_value()) {
        preferredTabId = ResolvePreferredMapTabIdForArea(focusArea);
    }
    if (preferredTabId.has_value()) {
        mapTrackerState.requestedTabId = *preferredTabId;
    }
}
std::unordered_map<std::string, RandomizerCheck> BuildGameCheckLookupByMapTrackerId(std::vector<MapIssueEntry>& warnings) {
    std::unordered_map<std::string, RandomizerCheck> checksByMapTrackerId;
    checksByMapTrackerId.reserve(RC_MAX);

    const auto& locationTable = Rando::StaticData::GetLocationTable();
    for (int checkIndex = static_cast<int>(RC_UNKNOWN_CHECK) + 1; checkIndex < static_cast<int>(RC_MAX); checkIndex++) {
        RandomizerCheck check = static_cast<RandomizerCheck>(checkIndex);
        const auto& location = locationTable[check];
        if (location.GetRandomizerCheck() != check) {
            continue;
        }
        std::string_view mapTrackerId = location.GetMapTrackerId();
        if (mapTrackerId.empty()) {
            continue;
        }

        std::string mapTrackerIdKey(mapTrackerId);
        auto existing = checksByMapTrackerId.find(mapTrackerIdKey);
        if (existing != checksByMapTrackerId.end() && existing->second != check) {
            warnings.push_back({ fmt::format("soh_id '{}' duplicate | kept: {} | dropped: {}", mapTrackerIdKey,
                                             GetCheckDisplayName(existing->second), GetCheckDisplayName(check)),
                                 "" });
            continue;
        }

        checksByMapTrackerId[mapTrackerIdKey] = check;
    }

    return checksByMapTrackerId;
}

std::vector<MapPackAreaFileRef> CollectMapPackAreaFiles(const std::string& resourcePathPrefix,
                                                        std::vector<MapIssueEntry>& warnings) {
    std::vector<MapPackAreaFileRef> areaFiles;
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        warnings.push_back({ "Archive manager unavailable — cannot list map pack area files", "" });
        return areaFiles;
    }

    auto archiveManager = context->GetResourceManager()->GetArchiveManager();
    std::unordered_set<std::string> seenResourcePaths;
    for (const char* extensionPattern : { "*.json", "*.jsonc" }) {
        std::string listPattern = BuildMapTrackerResourcePath(resourcePathPrefix,
                                                              fmt::format("{}/{}", CHECK_TRACKER_LOCATIONS_DIR,
                                                                          extensionPattern));
        auto matchedPaths = archiveManager->ListFiles(listPattern);
        if (matchedPaths == nullptr) {
            continue;
        }

        for (const auto& resourcePath : *matchedPaths) {
            if (!seenResourcePaths.insert(resourcePath).second) {
                continue;
            }
            areaFiles.push_back({ resourcePath, std::filesystem::path(resourcePath).filename().string() });
        }
    }

    std::sort(areaFiles.begin(), areaFiles.end(), [](const MapPackAreaFileRef& left, const MapPackAreaFileRef& right) {
        if (left.displayName == right.displayName) {
            return left.resourcePath < right.resourcePath;
        }
        return left.displayName < right.displayName;
    });

    return areaFiles;
}

std::vector<MapMarker> ParseMapMarkersFromPackAreas(
    MapTrackerState& state, const std::vector<MapPackAreaFileRef>& areaFiles,
    const std::unordered_map<std::string, RandomizerCheck>& checksByMapTrackerId,
    std::unordered_set<RandomizerCheck>& outLinkedChecks, std::vector<MapIssueEntry>& unresolvedLinks) {
    std::vector<MapMarker> mappedMarkers;
    outLinkedChecks.clear();
    std::unordered_set<std::string> seenMarkerKeys;
    bool hadSchemaError = false;

    std::string parseError;
    for (const auto& areaFile : areaFiles) {
        json areaJson;
        if (!LoadJsonFromArchiveResource(areaFile.resourcePath, areaJson, parseError)) {
            state.warnings.push_back(
                { fmt::format("{} — parse error: {}", areaFile.displayName, parseError), "" });
            continue;
        }

        if (!areaJson.is_object() || !areaJson.contains("checks") || !areaJson["checks"].is_array()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid area schema in " + areaFile.displayName,
                             "Expected an object with a \"checks\" array. Resource: " + areaFile.resourcePath);
            continue;
        }

        for (size_t checkIndex = 0; checkIndex < areaJson["checks"].size(); checkIndex++) {
            const auto& checkNode = areaJson["checks"][checkIndex];
            if (!checkNode.is_object()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                 fmt::format("checks[{}] must be an object.", checkIndex));
                continue;
            }

            std::string checkName;
            if (checkNode.contains("name")) {
                if (!checkNode["name"].is_string()) {
                    hadSchemaError = true;
                    AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                     fmt::format("checks[{}].name must be a string.", checkIndex));
                    continue;
                }
                checkName = checkNode["name"].get<std::string>();
            }

            if (!checkNode.contains("soh_id") || !checkNode["soh_id"].is_string()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                 fmt::format("Check \"{}\" is missing required string soh_id at checks[{}].",
                                             checkName, checkIndex));
                continue;
            }

            std::string sohId = TrimCopy(checkNode["soh_id"].get<std::string>());
            if (sohId.empty()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                 fmt::format("Check \"{}\" has an empty soh_id at checks[{}].", checkName,
                                             checkIndex));
                continue;
            }

            auto checkMatch = checksByMapTrackerId.find(sohId);
            if (checkMatch == checksByMapTrackerId.end()) {
                unresolvedLinks.push_back(
                    { fmt::format("soh_id: {} | pack: \"{}\" | {}", sohId, checkName, areaFile.displayName), "" });
                continue;
            }

            if (checkNode.contains("hint") && !checkNode["hint"].is_string()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                 fmt::format("Check \"{}\" has a non-string hint field.", checkName));
                continue;
            }

            if (checkNode.contains("hint") && checkNode["hint"].is_string()) {
                const std::string hintText = TrimCopy(checkNode["hint"].get<std::string>());
                if (!hintText.empty()) {
                    state.checkHints[checkMatch->second] = hintText;
                }
            }

            if (!checkNode.contains("map_locations") || !checkNode["map_locations"].is_array()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid check entry in " + areaFile.displayName,
                                 fmt::format("Check \"{}\" is missing required map_locations array.", checkName));
                continue;
            }

            std::vector<MapPlacement> placements =
                ExtractPlacementsFromNode(state, checkNode, areaFile.displayName, checkName.empty() ? sohId : checkName,
                                          hadSchemaError);
            if (placements.empty()) {
                state.warnings.push_back(
                    { fmt::format("{} | \"{}\" — no valid map_locations", areaFile.displayName, checkName), "" });
                continue;
            }

            for (const auto& placement : placements) {
                MapMarker marker;
                marker.check = checkMatch->second;
                marker.mapId = placement.mapId;
                if (marker.mapId.empty()) {
                    state.warnings.push_back(
                        { fmt::format("{} | \"{}\" — empty map_id", areaFile.displayName, checkName), "" });
                    continue;
                }
                marker.packCheckName = checkName.empty() ? sohId : checkName;
                marker.x = placement.x;
                marker.y = placement.y;
                marker.size = placement.size;

                int xQuantized = static_cast<int>(std::lround(marker.x * 100.0f));
                int yQuantized = static_cast<int>(std::lround(marker.y * 100.0f));
                int sizeQuantized = static_cast<int>(std::lround(marker.size * 100.0f));
                std::string markerKey =
                    fmt::format("{}|{}|{}|{}|{}", static_cast<int>(marker.check), marker.mapId,
                                xQuantized, yQuantized, sizeQuantized);
                if (!seenMarkerKeys.insert(markerKey).second) {
                    continue;
                }

                mappedMarkers.push_back(marker);
                outLinkedChecks.insert(marker.check);
            }
        }
    }

    if (hadSchemaError) {
        state.fatalErrors.push_back("One or more area files failed schema validation. Fix warnings and reload.");
    }

    return mappedMarkers;
}

struct MapsMetadataParseResult {
    std::unordered_map<std::string, std::string> mapNamesById;
    std::unordered_map<std::string, std::string> mapImagePathsById;
    std::unordered_map<std::string, std::string> mapGroupById;
    std::unordered_map<std::string, std::vector<MapLink>> mapLinksById;
    std::vector<std::string> orderedMapIds;
};

static MapTrackerState CreateMapTrackerLoadState() {
    MapTrackerState state;
    state.attemptedLoad = true;
    state.assetsRoot = GetMapTrackerAssetsRoot();
    return state;
}

static bool EnsureMapPackArchiveMounted(MapTrackerState& state, const std::filesystem::path& packFolderPath) {
    bool packFolderExists = std::filesystem::exists(packFolderPath) && std::filesystem::is_directory(packFolderPath);

    if (!packFolderExists) {
        state.fatalErrors.push_back("Map pack not found.");
        state.fatalErrors.push_back("Expected folder: " + packFolderPath.string());
        state.fatalErrors.push_back("Tried these candidate roots: " + BuildMapTrackerAssetsRootCandidatesSummary());
        state.fatalErrors.push_back("Put a map pack zip in mods/check_tracker_map_pack.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: pack folder not found. folder='{}'", packFolderPath.string());
        return false;
    }

    const std::filesystem::path packArchivePath = GetNewestMapPackZip(packFolderPath);
    if (packArchivePath.empty()) {
        state.fatalErrors.push_back("No map pack zip found.");
        state.fatalErrors.push_back("Expected at least one .zip in: " + packFolderPath.string());
        state.fatalErrors.push_back("When multiple zips exist, the newest modified file is used.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: no zip found in folder='{}'", packFolderPath.string());
        return false;
    }
    state.assetsRoot = packArchivePath;
    state.mountedArchivePath = packArchivePath;

    std::string mountError;
    std::string preferredPrefix = packArchivePath.stem().string();
    if (!EnsureMapTrackerZipArchiveMounted(packArchivePath, preferredPrefix, state.assetsArchiveMountRoot,
                                           state.resourcePathPrefix, mountError)) {
        state.fatalErrors.push_back("Failed to mount map pack zip archive: " + mountError);
        return false;
    }
    return true;
}

static bool LoadMapTrackerMetadataJson(MapTrackerState& state, json& outMapsJson, std::string& outMapsResourcePath) {
    std::string parseError;
    outMapsResourcePath = BuildMapTrackerResourcePath(state.resourcePathPrefix, CHECK_TRACKER_MAPS_JSON);
    if (!LoadJsonFromArchiveResource(outMapsResourcePath, outMapsJson, parseError)) {
        state.fatalErrors.push_back("Could not parse map metadata: " + parseError);
        state.fatalErrors.push_back("Tried resource path: " + outMapsResourcePath);
        return false;
    }
    return true;
}

static bool ParseMapMetadataEntries(MapTrackerState& state, const json& mapsJson, const std::string& mapsResourcePath,
                                    MapsMetadataParseResult& outMetadata) {
    if (!mapsJson.is_array()) {
        state.fatalErrors.push_back("Expected an array in maps.json.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: maps metadata root is not an array. resource='{}'", mapsResourcePath);
        return false;
    }

    bool hadSchemaError = false;
    for (size_t mapEntryIndex = 0; mapEntryIndex < mapsJson.size(); mapEntryIndex++) {
        const auto& mapEntry = mapsJson[mapEntryIndex];
        if (!mapEntry.is_object()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid maps.json entry",
                             fmt::format("Entry {} must be an object.", mapEntryIndex));
            continue;
        }

        if (!mapEntry.contains("id") || !mapEntry["id"].is_string()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid maps.json entry",
                             fmt::format("Entry {} is missing required string id.", mapEntryIndex));
            continue;
        }
        if (!mapEntry.contains("name") || !mapEntry["name"].is_string()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid maps.json entry",
                             fmt::format("Map \"{}\" is missing required string name.", mapEntry["id"].dump()));
            continue;
        }

        std::string mapId = TrimCopy(mapEntry["id"].get<std::string>());
        std::string mapName = TrimCopy(mapEntry["name"].get<std::string>());
        if (mapId.empty() || mapName.empty()) {
            hadSchemaError = true;
            AddSchemaWarning(state, "Invalid maps.json entry",
                             fmt::format("Entry {} has an empty id or name.", mapEntryIndex));
            continue;
        }

        if (outMetadata.mapNamesById.contains(mapId)) {
            AddSchemaWarning(state, "Duplicate map id in maps.json",
                             "Map id \"" + mapId + "\" is declared more than once.");
            hadSchemaError = true;
            continue;
        }
        outMetadata.orderedMapIds.push_back(mapId);
        outMetadata.mapNamesById[mapId] = mapName;

        std::string mapGroup;
        if (mapEntry.contains("group")) {
            if (!mapEntry["group"].is_string()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid maps.json entry",
                                 fmt::format("Map \"{}\" has a non-string group value.", mapId));
                continue;
            }
            mapGroup = TrimCopy(mapEntry["group"].get<std::string>());
        }
        outMetadata.mapGroupById[mapId] = mapGroup;

        if (mapEntry.contains("links")) {
            if (!mapEntry["links"].is_array()) {
                hadSchemaError = true;
                AddSchemaWarning(state, "Invalid link list in maps.json",
                                 fmt::format("Map \"{}\" has a non-array links value.", mapId));
                continue;
            }

            auto& links = outMetadata.mapLinksById[mapId];
            for (size_t linkIndex = 0; linkIndex < mapEntry["links"].size(); linkIndex++) {
                const auto& linkEntry = mapEntry["links"][linkIndex];
                if (!linkEntry.is_object()) {
                    hadSchemaError = true;
                    AddSchemaWarning(state, "Invalid link in map " + mapName,
                                     fmt::format("links[{}] must be an object.", linkIndex));
                    continue;
                }
                if (!linkEntry.contains("target_map_id") || !linkEntry["target_map_id"].is_string()) {
                    hadSchemaError = true;
                    AddSchemaWarning(state, "Invalid link in map " + mapName,
                                     fmt::format("links[{}] is missing required string target_map_id.", linkIndex));
                    continue;
                }

                MapLink link;
                link.targetMapId = TrimCopy(linkEntry["target_map_id"].get<std::string>());
                link.entranceIndex = ResolveMapLinkEntranceIndex(mapId, link.targetMapId);
                if (link.targetMapId.empty()) {
                    hadSchemaError = true;
                    AddSchemaWarning(state, "Invalid link in map " + mapName,
                                     "A links entry has an empty target_map_id.");
                    continue;
                }

                bool hasX = linkEntry.contains("x") && TryReadFloat(linkEntry["x"], link.x);
                bool hasY = linkEntry.contains("y") && TryReadFloat(linkEntry["y"], link.y);
                bool hasSize = true;
                if (linkEntry.contains("size")) {
                    hasSize = TryReadFloat(linkEntry["size"], link.size);
                }

                if (!hasX || !hasY || !hasSize) {
                    hadSchemaError = true;
                    AddSchemaWarning(state, "Invalid link coordinates in map " + mapName,
                                     "A links entry has invalid x/y/size values for target_map_id \"" +
                                         link.targetMapId + "\".");
                    continue;
                }

                links.push_back(std::move(link));
            }

            std::unordered_map<std::string, size_t> linkCountByTargetMapId;
            for (const auto& link : links) {
                linkCountByTargetMapId[link.targetMapId]++;
            }
            for (auto& link : links) {
                if (linkCountByTargetMapId[link.targetMapId] > 1) {
                    link.disableEntranceLogic = true;
                    link.entranceIndex = -1;
                }
            }
        }

        if (mapEntry.contains("img") && mapEntry["img"].is_string()) {
            outMetadata.mapImagePathsById[mapId] = mapEntry["img"].get<std::string>();
        } else {
            AddSchemaWarning(state, "Missing image path for map " + mapName,
                             "The map entry in maps.json is missing an \"img\" value.");
        }
    }
    SPDLOG_INFO("[CheckTrackerMapDiag] Parsed maps metadata. rawEntries={} uniqueMaps={} warnings={}",
                mapsJson.size(), outMetadata.orderedMapIds.size(), state.warnings.size());
    if (outMetadata.orderedMapIds.empty()) {
        state.fatalErrors.push_back("No maps were found in maps.json.");
        return false;
    }
    if (hadSchemaError) {
        state.fatalErrors.push_back("maps.json failed schema validation. Fix warnings and reload.");
        return false;
    }
    return true;
}

static bool BuildMapMarkersAndCheckLinks(MapTrackerState& state, std::vector<MapMarker>& outMappedMarkers) {
    std::vector<MapPackAreaFileRef> areaFiles = CollectMapPackAreaFiles(state.resourcePathPrefix, state.warnings);
    if (areaFiles.empty()) {
        state.fatalErrors.push_back("No area files found in map pack.");
        state.fatalErrors.push_back("Expected folder/resource pattern: " +
                                    BuildMapTrackerResourcePath(state.resourcePathPrefix,
                                                                std::string(CHECK_TRACKER_LOCATIONS_DIR) + "/*.json"));
        return false;
    }

    std::unordered_map<std::string, RandomizerCheck> checksByMapTrackerId =
        BuildGameCheckLookupByMapTrackerId(state.warnings);
    if (checksByMapTrackerId.empty()) {
        state.fatalErrors.push_back("No in-game checks were available for soh_id mapping.");
        return false;
    }
    outMappedMarkers =
        ParseMapMarkersFromPackAreas(state, areaFiles, checksByMapTrackerId, state.linkedChecks, state.unresolvedLinks);
    if (!state.fatalErrors.empty()) {
        return false;
    }

    std::vector<CheckDescriptor> descriptors = BuildVisibleCheckDescriptors();
    if (descriptors.empty()) {
        state.fatalErrors.push_back("No visible checks available to map. Load a randomizer save first.");
        return false;
    }
    for (const auto& descriptor : descriptors) {
        if (!state.linkedChecks.contains(descriptor.check)) {
            state.unassignedCheckIds.push_back(descriptor.check);
            state.unresolvedLinks.push_back(
                { fmt::format("{} | {} | no pack marker", descriptor.checkDisplayName,
                              RandomizerCheckObjects::GetRCAreaName(descriptor.area)),
                  "" });
        }
    }

    if (IS_ARCHIPELAGO) {
        for (const std::string& apLocationName : CheckTracker::GetArchipelagoScoutedUnresolvedLocationNames()) {
            state.archipelagoScoutedWithoutMapId.push_back({ fmt::format("AP (unmapped): {}", apLocationName), "" });
        }

        for (RandomizerCheck rc : CheckTracker::GetArchipelagoScoutedChecks()) {
            if (!IsVisibleInCheckTracker(rc)) {
                continue;
            }
            if (!GetGameCheckMapTrackerId(rc).empty()) {
                continue;
            }

            Rando::Location* location = Rando::StaticData::GetLocation(rc);
            state.archipelagoScoutedWithoutMapId.push_back(
                { fmt::format("{} | {} | AP: {}", GetCheckDisplayName(rc),
                              RandomizerCheckObjects::GetRCAreaName(location->GetArea()), location->GetName()),
                  "" });
        }
    }
    std::sort(state.unassignedCheckIds.begin(), state.unassignedCheckIds.end(),
              [](RandomizerCheck left, RandomizerCheck right) {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    state.unassignedCheckIds.erase(std::unique(state.unassignedCheckIds.begin(), state.unassignedCheckIds.end()),
                                   state.unassignedCheckIds.end());

    SPDLOG_INFO("[CheckTrackerMapDiag] soh_id mapping summary. mappedMarkers={} linkedChecks={} unresolved={} unassigned={}",
                outMappedMarkers.size(), state.linkedChecks.size(), state.unresolvedLinks.size(),
                state.unassignedCheckIds.size());
    return true;
}

static void BuildMapTabsFromMetadata(MapTrackerState& state, const MapsMetadataParseResult& metadata) {
    for (const auto& mapId : metadata.orderedMapIds) {
        MapTabData tab;
        tab.mapId = mapId;
        tab.mapName = metadata.mapNamesById.at(mapId);
        if (metadata.mapGroupById.contains(mapId)) {
            tab.groupName = metadata.mapGroupById.at(mapId);
        }
        if (metadata.mapLinksById.contains(mapId)) {
            tab.links = metadata.mapLinksById.at(mapId);
        }
        if (metadata.mapImagePathsById.contains(mapId)) {
            tab.imageRelativePath = metadata.mapImagePathsById.at(mapId);
            tab.imageResourcePath = BuildMapTrackerResourcePath(state.resourcePathPrefix, tab.imageRelativePath);
        } else {
            tab.imageError = "No image entry found in maps.json for map_id \"" + mapId + "\".";
        }

        state.tabIndexById[tab.mapId] = state.tabs.size();
        state.tabs.push_back(std::move(tab));
    }

    for (const auto& tab : state.tabs) {
        for (const auto& link : tab.links) {
            if (!state.tabIndexById.contains(link.targetMapId)) {
                state.warnings.push_back(
                    { fmt::format("{} → map_id '{}' (tab missing)", tab.mapName, link.targetMapId), "" });
            }
        }
    }
}

static void BuildMapTabGroups(MapTrackerState& state) {
    bool hasNamedGroups = false;
    for (auto& tab : state.tabs) {
        tab.groupName = TrimCopy(tab.groupName);
        if (!tab.groupName.empty()) {
            hasNamedGroups = true;
        }
    }

    if (hasNamedGroups) {
        for (auto& tab : state.tabs) {
            if (tab.groupName.empty()) {
                tab.groupName = "Others";
            }
        }
    }

    for (size_t tabIndex = 0; tabIndex < state.tabs.size(); tabIndex++) {
        const std::string groupName = state.tabs[tabIndex].groupName;
        if (groupName.empty()) {
            continue;
        }
        if (!state.tabIndicesByGroup.contains(groupName)) {
            state.mapGroups.push_back(groupName);
        }
        state.tabIndicesByGroup[groupName].push_back(static_cast<int>(tabIndex));
    }
    for (const auto& [groupName, groupTabIndices] : state.tabIndicesByGroup) {
        if (!groupTabIndices.empty()) {
            state.lastSelectedTabByGroup[groupName] = groupTabIndices.front();
        }
    }
    if (!state.mapGroups.empty()) {
        state.selectedGroupName = state.mapGroups.front();
    } else {
        state.selectedGroupName.clear();
    }
}

static void LinkMapMarkersToTabs(MapTrackerState& state, const std::vector<MapMarker>& mappedMarkers) {
    for (const auto& marker : mappedMarkers) {
        if (!state.tabIndexById.contains(marker.mapId)) {
            state.unresolvedLinks.push_back(
                { fmt::format("{} | map_id: {} (tab missing)", GetCheckDisplayName(marker.check), marker.mapId), "" });
            continue;
        }
        int markerTabIndex = static_cast<int>(state.tabIndexById[marker.mapId]);
        state.tabs[static_cast<size_t>(markerTabIndex)].markers.push_back(marker);
    }
}

static bool LoadMapTabTextures(MapTrackerState& state, uint64_t loadGeneration) {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui == nullptr) {
        state.fatalErrors.push_back("Could not access GUI texture loader.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: GUI texture loader was null.");
        return false;
    }

    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        state.fatalErrors.push_back("Could not access archive manager for map texture resources.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: archive manager unavailable.");
        return false;
    }
    auto archiveManager = context->GetResourceManager()->GetArchiveManager();

    for (auto& tab : state.tabs) {
        std::sort(tab.markers.begin(), tab.markers.end(), [](const MapMarker& left, const MapMarker& right) {
            if (left.check == right.check) {
                return left.packCheckName < right.packCheckName;
            }
            return left.check < right.check;
        });

        if (!tab.imageError.empty()) {
            continue;
        }

        if (!archiveManager->HasFile(tab.imageResourcePath)) {
            tab.imageError = "Image resource not indexed in archive: " + tab.imageResourcePath +
                             " | Archive mount root: " + state.assetsArchiveMountRoot.string();
            continue;
        }

        tab.textureName = BuildMapTextureName(loadGeneration, tab.mapId);
        if (gui->HasTextureByName(tab.textureName)) {
            gui->UnloadTexture(tab.textureName);
        }

        try {
            gui->LoadTextureFromRawImage(tab.textureName, tab.imageResourcePath);
            tab.texture = gui->GetTextureByName(tab.textureName);
            tab.textureSize = gui->GetTextureSize(tab.textureName);
            tab.imageLoaded = tab.texture != 0 && tab.textureSize.x > 0.0f && tab.textureSize.y > 0.0f;
            if (!tab.imageLoaded) {
                tab.imageError = "Failed to load map texture from resource path: " + tab.imageResourcePath;
            }
        } catch (...) {
            tab.imageError = "Failed to load map texture from resource path: " + tab.imageResourcePath;
        }
    }
    return true;
}

static void FinalizeMapTrackerLoadSuccess(MapTrackerState& state) {
    state.loaded = true;
}

static void LogMapTrackerLoadSuccess(const MapTrackerState& state, const std::chrono::steady_clock::time_point& loadStartTime) {
    SPDLOG_INFO("[CheckTrackerMapDiag] Load completed in {} ms. tabs={} warnings={} fatalErrors={}",
                GetElapsedMilliseconds(loadStartTime), state.tabs.size(), state.warnings.size(), state.fatalErrors.size());
}

void LoadMapTrackerData() {
    const auto loadStartTime = std::chrono::steady_clock::now();
    MapTrackerState previousState = std::move(mapTrackerState);
    MapTrackerState loadedState = CreateMapTrackerLoadState();
    const std::filesystem::path packFolderPath = loadedState.assetsRoot;
    const uint64_t textureLoadGeneration = mapTrackerTextureLoadGeneration++;

    SPDLOG_INFO("[CheckTrackerMapDiag] Load start. assets='{}' candidates='{}'", loadedState.assetsRoot.string(),
                BuildMapTrackerAssetsRootCandidatesSummary());

    bool loadSucceeded = false;
    json mapsJson;
    std::string mapsResourcePath;
    MapsMetadataParseResult metadata;
    std::vector<MapMarker> mappedMarkers;

    if (EnsureMapPackArchiveMounted(loadedState, packFolderPath) &&
        LoadMapTrackerMetadataJson(loadedState, mapsJson, mapsResourcePath) &&
        ParseMapMetadataEntries(loadedState, mapsJson, mapsResourcePath, metadata) &&
        BuildMapMarkersAndCheckLinks(loadedState, mappedMarkers)) {
        BuildMapTabsFromMetadata(loadedState, metadata);
        BuildMapTabGroups(loadedState);
        LinkMapMarkersToTabs(loadedState, mappedMarkers);
        loadSucceeded = LoadMapTabTextures(loadedState, textureLoadGeneration);
    }

    if (!loadSucceeded) {
        UnloadMapTrackerResources(loadedState, true);
        if (previousState.loaded) {
            AppendReloadFailureWarning(previousState, loadedState);
            mapTrackerState = std::move(previousState);
        } else {
            mapTrackerState = std::move(loadedState);
            InvalidateMapTrackerRenderCache();
        }
        return;
    }

    PreserveMapTrackerSessionState(previousState, loadedState);
    UnloadMapTrackerResources(previousState, true);
    FinalizeMapTrackerLoadSuccess(loadedState);
    mapTrackerState = std::move(loadedState);
    InvalidateMapTrackerRenderCache();
    LogMapTrackerLoadSuccess(mapTrackerState, loadStartTime);
}

} // namespace CheckTracker

