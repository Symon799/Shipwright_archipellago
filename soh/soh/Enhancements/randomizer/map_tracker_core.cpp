#include "map_tracker_internal.h"
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

namespace MapIds {
inline constexpr const char* BottomOfTheWell = "bottom_of_the_well";
inline constexpr const char* DekuTree = "deku_tree";
inline constexpr const char* DesertColossus = "desert_colossus";
inline constexpr const char* DodongosCavern = "dodongos_cavern";
inline constexpr const char* Dmc = "dmc";
inline constexpr const char* Dmt = "dmt";
inline constexpr const char* FireTemple = "fire_temple";
inline constexpr const char* ForestTemple = "forest_temple";
inline constexpr const char* GanonsCastle = "ganons_castle";
inline constexpr const char* GanonsTower = "ganons_tower";
inline constexpr const char* GerudoFortress = "gerudo_fortress";
inline constexpr const char* GerudoTrainingGround = "gerudo_training_ground";
inline constexpr const char* GerudoValley = "gerudo_valley";
inline constexpr const char* GoronCity = "goron_city";
inline constexpr const char* Graveyard = "graveyard";
inline constexpr const char* HyruleCastle = "hyrule_castle";
inline constexpr const char* HyruleFields = "hyrule_fields";
inline constexpr const char* IceCavern = "ice_cavern";
inline constexpr const char* JabuJabusBelly = "jabu_jabus_belly";
inline constexpr const char* KakarikoVillage = "kakariko_village";
inline constexpr const char* KokiriForest = "kokiri_forest";
inline constexpr const char* LakeHylia = "lake_hylia";
inline constexpr const char* LonLonRanch = "lon_lon_ranch";
inline constexpr const char* LostWoods = "lost_woods";
inline constexpr const char* Market = "market";
inline constexpr const char* Overworld = "overworld";
inline constexpr const char* SacredForestMeadow = "sfm";
inline constexpr const char* ShadowTemple = "shadow_temple";
inline constexpr const char* SpiritTemple = "spirit_temple";
inline constexpr const char* TempleOfTime = "temple_of_time";
inline constexpr const char* Wasteland = "wasteland";
inline constexpr const char* WaterTemple = "water_temple";
inline constexpr const char* ZoraRiver = "zora_river";
inline constexpr const char* ZorasDomain = "zoras_domain";
inline constexpr const char* ZorasFountain = "zoras_fountain";
} // namespace MapIds

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

bool LoadJsonWithComments(const std::filesystem::path& filePath, json& outJson, std::string& outError) {
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        outError = "Could not open file";
        return false;
    }

    std::stringstream buffer;
    buffer << inputFile.rdbuf();
    try {
        outJson = json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception& exception) {
        outError = exception.what();
        return false;
    }

    return true;
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
        return left.filename().string() < right.filename().string();
    });
    return zipFiles;
}

std::filesystem::path GetFirstMapPackZip(const std::filesystem::path& packFolderPath) {
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

bool LoadJsonFromMapPack(const std::filesystem::path& diskPath, const std::string& resourcePath, json& outJson,
                         std::string& outError) {
    if (!diskPath.empty()) {
        return LoadJsonWithComments(diskPath, outJson, outError);
    }
    return LoadJsonFromArchiveResource(resourcePath, outJson, outError);
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
    if (!archiveManager->HasFile(preferredProbePath) && !archiveManager->HasFile(CHECK_TRACKER_MAPS_JSON)) {
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

bool EnsureMapTrackerArchiveMounted(const std::filesystem::path& assetsRoot, std::filesystem::path& outArchiveMountRoot,
                                    std::string& outResourcePathPrefix, std::string& outError) {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        outError = "Resource manager is unavailable.";
        return false;
    }

    outArchiveMountRoot = assetsRoot.parent_path();
    if (outArchiveMountRoot.empty() || !std::filesystem::exists(outArchiveMountRoot) ||
        !std::filesystem::is_directory(outArchiveMountRoot)) {
        outError = "Invalid archive mount directory: " + outArchiveMountRoot.string();
        return false;
    }

    outResourcePathPrefix = assetsRoot.filename().string();
    if (outResourcePathPrefix.empty()) {
        outError = "Could not derive resource path prefix from assets root: " + assetsRoot.string();
        return false;
    }

    auto archiveManager = context->GetResourceManager()->GetArchiveManager();
    const std::string mapsProbePath =
        (std::filesystem::path(outResourcePathPrefix) / CHECK_TRACKER_MAPS_JSON).lexically_normal().generic_string();

    if (!archiveManager->HasFile(mapsProbePath)) {
        auto archive = archiveManager->AddArchive(outArchiveMountRoot.string());
        if (archive == nullptr) {
            outError = "Failed to mount archive folder: " + outArchiveMountRoot.string();
            return false;
        }
    }

    if (!archiveManager->HasFile(mapsProbePath)) {
        outError = "Map pack is not indexed after mount. Missing virtual file: " + mapsProbePath;
        return false;
    }

    return true;
}

bool ValidateMapImageFile(const std::filesystem::path& imagePath, std::string& outError) {
    if (!std::filesystem::exists(imagePath)) {
        outError = "Image file not found: " + imagePath.string();
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(imagePath, errorCode)) {
        outError = "Image path is not a regular file: " + imagePath.string();
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

std::vector<MapPlacement> ExtractPlacementsFromNode(const json& node, const std::string& sourceFile,
                                                    std::vector<MapIssueEntry>& warnings) {
    std::vector<MapPlacement> placements;
    if (!node.contains("map_locations") || !node["map_locations"].is_array()) {
        return placements;
    }

    for (const auto& mapLoc : node["map_locations"]) {
        if (!mapLoc.is_object()) {
            continue;
        }

        if (!mapLoc.contains("map_id") || !mapLoc["map_id"].is_string()) {
            warnings.push_back({ "Invalid map location in " + sourceFile,
                                 "One node is missing a valid \"map_id\" string in map_locations." });
            continue;
        }

        MapPlacement placement;
        placement.mapId = TrimCopy(mapLoc["map_id"].get<std::string>());
        if (placement.mapId.empty()) {
            warnings.push_back({ "Invalid map location in " + sourceFile, "A map_locations entry has an empty map_id." });
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
            warnings.push_back(
                { "Invalid map coordinates in " + sourceFile,
                  "A map_locations entry has invalid x/y/size values for map_id \"" + placement.mapId + "\"." });
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
    if (unloadTextures) {
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        if (gui != nullptr) {
            for (const auto& tab : mapTrackerState.tabs) {
                if (!tab.textureName.empty() && gui->HasTextureByName(tab.textureName)) {
                    gui->UnloadTexture(tab.textureName);
                }
            }
        }
    }
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

static const LocationAccess* FindLocationAccessInParentRegion(RandomizerCheck rc, RandomizerRegion parentRegion) {
    if (parentRegion == RR_NONE || parentRegion >= RR_MAX) {
        return nullptr;
    }

    for (const auto& locationInRegion : areaTable[parentRegion].locations) {
        if (locationInRegion.GetLocation() == rc) {
            return &locationInRegion;
        }
    }

    return nullptr;
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
        if (evaluateAtNight) {
            conditionsMet = locationAccess.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtNight);
        } else {
            conditionsMet = locationAccess.CheckConditionAtAgeTime(logicRef->IsAdult, logicRef->AtDay);
        }
    } else {
        if (evaluateAtNight) {
            conditionsMet = locationAccess.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtNight);
        } else {
            conditionsMet = locationAccess.CheckConditionAtAgeTime(logicRef->IsChild, logicRef->AtDay);
        }
    }

    logicRef->IsChild = previousIsChild;
    logicRef->IsAdult = previousIsAdult;
    logicRef->AtDay = previousAtDay;
    logicRef->AtNight = previousAtNight;
    logicRef->CurrentRegionKey = previousRegionKey;
    logicRef->CurrentCheckKey = previousCheckKey;

    return conditionsMet;
}

static CheckAgeTimeAvailabilityInfo EvaluateCheckAgeTimeAvailability(RandomizerCheck rc) {
    auto* itemLocation = OTRGlobals::Instance->gRandoContext->GetItemLocation(rc);
    if (itemLocation == nullptr) {
        return {};
    }

    RandomizerRegion parentRegion = itemLocation->GetParentRegionKey();
    const LocationAccess* locationAccess = FindLocationAccessInParentRegion(rc, parentRegion);
    if (locationAccess == nullptr) {
        return {};
    }

    if (parentRegion == RR_NONE || parentRegion >= RR_MAX) {
        return {};
    }

    Region& parent = areaTable[parentRegion];

    auto evaluateCombo = [&](bool parentHasAccess, bool evaluateAsAdult, bool evaluateAtNight) {
        if (!parentHasAccess) {
            return false;
        }
        return EvaluateLocationConditionAtAgeTime(*locationAccess, parentRegion, rc, evaluateAsAdult, evaluateAtNight);
    };

    return BuildAgeTimeAvailabilityInfo(evaluateCombo(parent.childDay, false, false),
                                        evaluateCombo(parent.childNight, false, true),
                                        evaluateCombo(parent.adultDay, true, false),
                                        evaluateCombo(parent.adultNight, true, true));
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

bool IsCheckAvailableButWrongAgeOrTime(RandomizerCheck rc) {
    CheckAgeTimeAvailabilityInfo availabilityInfo = EvaluateCheckAgeTimeAvailability(rc);
    return availabilityInfo.canDoAtAll && !availabilityInfo.canDoNow;
}

std::string GetCheckRequirementSummary(RandomizerCheck rc) {
    return BuildCheckRequirementSummary(EvaluateCheckAgeTimeAvailability(rc));
}

static std::optional<std::string> ResolveFirstExistingMapTabId(
    const std::initializer_list<const char*>& preferredMapIds) {
    for (const char* preferredMapId : preferredMapIds) {
        if (mapTrackerState.tabIndexById.contains(preferredMapId)) {
            return std::string(preferredMapId);
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

static int16_t ResolveMapLinkEntranceIndex(std::string_view sourceMapId, std::string_view targetMapId) {
    static const std::array<std::tuple<std::string_view, std::string_view, int16_t>, 6> entranceIndexByMapPair = { {
        { MapIds::LostWoods, MapIds::ZoraRiver, ENTR_ZORAS_RIVER_UNDERWATER_SHORTCUT },
        { MapIds::ZoraRiver, MapIds::LostWoods, ENTR_LOST_WOODS_UNDERWATER_SHORTCUT },
        { MapIds::LostWoods, MapIds::GoronCity, ENTR_GORON_CITY_TUNNEL_SHORTCUT },
        { MapIds::GoronCity, MapIds::LostWoods, ENTR_LOST_WOODS_TUNNEL_SHORTCUT },
        { MapIds::LostWoods, MapIds::SacredForestMeadow, ENTR_SACRED_FOREST_MEADOW_SOUTH_EXIT },
        { MapIds::SacredForestMeadow, MapIds::LostWoods, ENTR_LOST_WOODS_NORTH_EXIT },
    } };
    static const std::array<std::pair<std::string_view, int16_t>, 14> entranceIndexByTargetMap = { {
        { MapIds::DekuTree, ENTR_DEKU_TREE_ENTRANCE },
        { MapIds::DodongosCavern, ENTR_DODONGOS_CAVERN_ENTRANCE },
        { MapIds::JabuJabusBelly, ENTR_JABU_JABU_ENTRANCE },
        { MapIds::ForestTemple, ENTR_FOREST_TEMPLE_ENTRANCE },
        { MapIds::FireTemple, ENTR_FIRE_TEMPLE_ENTRANCE },
        { MapIds::WaterTemple, ENTR_WATER_TEMPLE_ENTRANCE },
        { MapIds::SpiritTemple, ENTR_SPIRIT_TEMPLE_ENTRANCE },
        { MapIds::ShadowTemple, ENTR_SHADOW_TEMPLE_ENTRANCE },
        { MapIds::BottomOfTheWell, ENTR_BOTTOM_OF_THE_WELL_ENTRANCE },
        { MapIds::IceCavern, ENTR_ICE_CAVERN_ENTRANCE },
        { MapIds::GerudoTrainingGround, ENTR_GERUDO_TRAINING_GROUND_ENTRANCE },
        { MapIds::GanonsCastle, ENTR_INSIDE_GANONS_CASTLE_ENTRANCE },
        { MapIds::GanonsTower, ENTR_INSIDE_GANONS_CASTLE_ENTRANCE },
        { MapIds::TempleOfTime, ENTR_TEMPLE_OF_TIME_ENTRANCE },
    } };

    for (const auto& [entrySourceMapId, entryTargetMapId, entranceIndex] : entranceIndexByMapPair) {
        if (sourceMapId == entrySourceMapId && targetMapId == entryTargetMapId) {
            return entranceIndex;
        }
    }

    for (const auto& [entryTargetMapId, entranceIndex] : entranceIndexByTargetMap) {
        if (targetMapId == entryTargetMapId) {
            return entranceIndex;
        }
    }

    return -1;
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
            warnings.push_back(
                { "Duplicate in-game map tracker id",
                  fmt::format("Map tracker id '{}' maps to both '{}' and '{}'. Keeping '{}'.", mapTrackerIdKey,
                              GetCheckDisplayName(existing->second), GetCheckDisplayName(check),
                              GetCheckDisplayName(existing->second)) });
            continue;
        }

        checksByMapTrackerId[mapTrackerIdKey] = check;
    }

    return checksByMapTrackerId;
}

std::vector<MapPackAreaFileRef> CollectMapPackAreaFiles(const std::filesystem::path& packFolderPath, bool usingArchivePack,
                                                        const std::string& resourcePathPrefix,
                                                        std::vector<MapIssueEntry>& warnings) {
    std::vector<MapPackAreaFileRef> areaFiles;
    if (!usingArchivePack) {
        std::filesystem::path areaDirPath = packFolderPath / CHECK_TRACKER_LOCATIONS_DIR;
        if (!std::filesystem::exists(areaDirPath) || !std::filesystem::is_directory(areaDirPath)) {
            warnings.push_back({ "Missing areas directory", "Expected directory: " + areaDirPath.string() });
            return areaFiles;
        }

        for (const auto& directoryEntry : std::filesystem::directory_iterator(areaDirPath)) {
            if (!directoryEntry.is_regular_file()) {
                continue;
            }

            std::filesystem::path filePath = directoryEntry.path();
            const std::filesystem::path extension = filePath.extension();
            if (extension != ".json" && extension != ".jsonc") {
                continue;
            }

            std::string relativeResourcePath =
                (std::filesystem::path(CHECK_TRACKER_LOCATIONS_DIR) / filePath.filename()).lexically_normal().generic_string();
            areaFiles.push_back(
                { filePath, BuildMapTrackerResourcePath(resourcePathPrefix, relativeResourcePath), filePath.filename().string() });
        }
    } else {
        auto context = Ship::Context::GetInstance();
        if (context == nullptr || context->GetResourceManager() == nullptr ||
            context->GetResourceManager()->GetArchiveManager() == nullptr) {
            warnings.push_back({ "Archive manager unavailable", "Could not list map pack area files from archive." });
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
                areaFiles.push_back({ {}, resourcePath, std::filesystem::path(resourcePath).filename().string() });
            }
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
    const std::vector<MapPackAreaFileRef>& areaFiles,
    const std::unordered_map<std::string, RandomizerCheck>& checksByMapTrackerId,
    std::unordered_set<RandomizerCheck>& outLinkedChecks, std::vector<MapIssueEntry>& warnings,
    std::vector<MapIssueEntry>& unresolvedLinks) {
    std::vector<MapMarker> mappedMarkers;
    outLinkedChecks.clear();
    std::unordered_set<std::string> seenMarkerKeys;

    std::string parseError;
    for (const auto& areaFile : areaFiles) {
        json areaJson;
        if (!LoadJsonFromMapPack(areaFile.diskPath, areaFile.resourcePath, areaJson, parseError)) {
            warnings.push_back(
                { "Failed to parse area file " + areaFile.displayName,
                  "Parse error: " + parseError + " | Resource: " + areaFile.resourcePath });
            continue;
        }

        if (!areaJson.is_object() || !areaJson.contains("checks") || !areaJson["checks"].is_array()) {
            warnings.push_back(
                { "Invalid area schema in " + areaFile.displayName,
                  "Expected an object with a \"checks\" array. Resource: " + areaFile.resourcePath });
            continue;
        }

        for (const auto& checkNode : areaJson["checks"]) {
            if (!checkNode.is_object()) {
                continue;
            }

            std::string checkName = checkNode.value("name", "");
            std::string sohId = TrimCopy(checkNode.value("soh_id", ""));
            if (sohId.empty()) {
                unresolvedLinks.push_back({ "Missing soh_id in " + areaFile.displayName,
                                            "Check \"" + checkName + "\" is missing a valid soh_id." });
                continue;
            }

            auto checkMatch = checksByMapTrackerId.find(sohId);
            if (checkMatch == checksByMapTrackerId.end()) {
                unresolvedLinks.push_back(
                    { "Unmapped soh_id: " + sohId,
                      "Check \"" + checkName + "\" in " + areaFile.displayName +
                          " has a soh_id that was not found in in-game checks." });
                continue;
            }

            if (checkNode.contains("hint") && checkNode["hint"].is_string()) {
                const std::string hintText = TrimCopy(checkNode["hint"].get<std::string>());
                if (!hintText.empty()) {
                    mapTrackerState.checkHints[checkMatch->second] = hintText;
                }
            }

            std::vector<MapPlacement> placements = ExtractPlacementsFromNode(checkNode, areaFile.displayName, warnings);
            if (placements.empty()) {
                warnings.push_back(
                    { "Missing map_locations in " + areaFile.displayName,
                      "Check \"" + checkName + "\" has no valid map_locations entries." });
                continue;
            }

            for (const auto& placement : placements) {
                MapMarker marker;
                marker.check = checkMatch->second;
                marker.mapId = placement.mapId;
                if (marker.mapId.empty()) {
                    warnings.push_back(
                        { "Invalid marker map id in " + areaFile.displayName,
                          "Check \"" + checkName + "\" has an empty map_id." });
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

    return mappedMarkers;
}

struct MapsMetadataParseResult {
    std::unordered_map<std::string, std::string> mapNamesById;
    std::unordered_map<std::string, std::string> mapImagePathsById;
    std::unordered_map<std::string, std::string> mapGroupById;
    std::unordered_map<std::string, std::vector<MapLink>> mapLinksById;
    std::vector<std::string> orderedMapIds;
};

static void InitializeMapTrackerLoadState() {
    ResetMapTrackerState(true);
    mapTrackerState.attemptedLoad = true;
    mapTrackerState.assetsRoot = GetMapTrackerAssetsRoot();
}

static bool EnsureMapPackArchiveMounted(const std::filesystem::path& packFolderPath) {
    bool packFolderExists = std::filesystem::exists(packFolderPath) && std::filesystem::is_directory(packFolderPath);

    if (!packFolderExists) {
        mapTrackerState.fatalErrors.push_back("Map pack not found.");
        mapTrackerState.fatalErrors.push_back("Expected folder: " + packFolderPath.string());
        mapTrackerState.fatalErrors.push_back("Tried these candidate roots: " + BuildMapTrackerAssetsRootCandidatesSummary());
        mapTrackerState.fatalErrors.push_back("Put a map pack zip in mods/check_tracker_map_pack.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: pack folder not found. folder='{}'", packFolderPath.string());
        return false;
    }

    const std::filesystem::path packArchivePath = GetFirstMapPackZip(packFolderPath);
    if (packArchivePath.empty()) {
        mapTrackerState.fatalErrors.push_back("No map pack zip found.");
        mapTrackerState.fatalErrors.push_back("Expected at least one .zip in: " + packFolderPath.string());
        mapTrackerState.fatalErrors.push_back("Any zip filename is supported.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: no zip found in folder='{}'", packFolderPath.string());
        return false;
    }
    mapTrackerState.usingArchivePack = true;
    mapTrackerState.assetsRoot = packArchivePath;

    std::string mountError;
    std::string preferredPrefix = packArchivePath.stem().string();
    if (!EnsureMapTrackerZipArchiveMounted(packArchivePath, preferredPrefix, mapTrackerState.assetsArchiveMountRoot,
                                           mapTrackerState.resourcePathPrefix, mountError)) {
        mapTrackerState.fatalErrors.push_back("Failed to mount map pack zip archive: " + mountError);
        return false;
    }
    return true;
}

static bool LoadMapTrackerMetadataJson(const std::filesystem::path& packFolderPath, json& outMapsJson,
                                       std::filesystem::path& outMapsDiskPath, std::string& outMapsResourcePath) {
    std::string parseError;
    outMapsResourcePath = BuildMapTrackerResourcePath(mapTrackerState.resourcePathPrefix, CHECK_TRACKER_MAPS_JSON);
    if (!LoadJsonFromMapPack(outMapsDiskPath, outMapsResourcePath, outMapsJson, parseError)) {
        mapTrackerState.fatalErrors.push_back("Could not parse map metadata: " + parseError);
        mapTrackerState.fatalErrors.push_back("Tried disk path: " + (packFolderPath / CHECK_TRACKER_MAPS_JSON).string());
        mapTrackerState.fatalErrors.push_back("Tried resource path: " + outMapsResourcePath);
        return false;
    }
    return true;
}

static bool ParseMapMetadataEntries(const json& mapsJson, const std::string& mapsResourcePath,
                                    const std::filesystem::path& mapsDiskPath, MapsMetadataParseResult& outMetadata) {
    if (!mapsJson.is_array()) {
        mapTrackerState.fatalErrors.push_back("Expected an array in maps.json.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: maps metadata root is not an array. resource='{}' disk='{}'",
                     mapsResourcePath, mapsDiskPath.string());
        return false;
    }

    for (const auto& mapEntry : mapsJson) {
        if (!mapEntry.is_object() || !mapEntry.contains("id") || !mapEntry["id"].is_string() || !mapEntry.contains("name") ||
            !mapEntry["name"].is_string()) {
            continue;
        }

        std::string mapId = TrimCopy(mapEntry["id"].get<std::string>());
        std::string mapName = TrimCopy(mapEntry["name"].get<std::string>());
        if (mapId.empty() || mapName.empty()) {
            continue;
        }

        if (outMetadata.mapNamesById.contains(mapId)) {
            mapTrackerState.warnings.push_back(
                { "Duplicate map id in maps.json", "Map id \"" + mapId + "\" is declared more than once." });
            continue;
        }
        outMetadata.orderedMapIds.push_back(mapId);
        outMetadata.mapNamesById[mapId] = mapName;

        std::string mapGroup;
        if (mapEntry.contains("group") && mapEntry["group"].is_string()) {
            mapGroup = TrimCopy(mapEntry["group"].get<std::string>());
        }
        outMetadata.mapGroupById[mapId] = mapGroup;

        if (mapEntry.contains("links") && mapEntry["links"].is_array()) {
            auto& links = outMetadata.mapLinksById[mapId];
            for (const auto& linkEntry : mapEntry["links"]) {
                if (!linkEntry.is_object() || !linkEntry.contains("target_map_id") ||
                    !linkEntry["target_map_id"].is_string()) {
                    mapTrackerState.warnings.push_back(
                        { "Invalid link in map " + mapName,
                          "A links entry is missing a valid \"target_map_id\" string." });
                    continue;
                }

                MapLink link;
                link.targetMapId = TrimCopy(linkEntry["target_map_id"].get<std::string>());
                link.entranceIndex = ResolveMapLinkEntranceIndex(mapId, link.targetMapId);
                if (link.targetMapId.empty()) {
                    mapTrackerState.warnings.push_back(
                        { "Invalid link in map " + mapName, "A links entry has an empty target_map_id." });
                    continue;
                }

                bool hasX = linkEntry.contains("x") && TryReadFloat(linkEntry["x"], link.x);
                bool hasY = linkEntry.contains("y") && TryReadFloat(linkEntry["y"], link.y);
                bool hasSize = true;
                if (linkEntry.contains("size")) {
                    hasSize = TryReadFloat(linkEntry["size"], link.size);
                }

                if (!hasX || !hasY || !hasSize) {
                    mapTrackerState.warnings.push_back(
                        { "Invalid link coordinates in map " + mapName,
                          "A links entry has invalid x/y/size values for target_map_id \"" + link.targetMapId + "\"." });
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
            mapTrackerState.warnings.push_back({ "Missing image path for map " + mapName,
                                                 "The map entry in maps.json is missing an \"img\" value." });
        }
    }
    SPDLOG_INFO("[CheckTrackerMapDiag] Parsed maps metadata. rawEntries={} uniqueMaps={} warnings={}",
                mapsJson.size(), outMetadata.orderedMapIds.size(), mapTrackerState.warnings.size());
    if (outMetadata.orderedMapIds.empty()) {
        mapTrackerState.fatalErrors.push_back("No maps were found in maps.json.");
        return false;
    }
    return true;
}

static bool BuildMapMarkersAndCheckLinks(const std::filesystem::path& packFolderPath,
                                         std::vector<MapMarker>& outMappedMarkers) {
    std::vector<MapPackAreaFileRef> areaFiles =
        CollectMapPackAreaFiles(packFolderPath, mapTrackerState.usingArchivePack, mapTrackerState.resourcePathPrefix,
                                mapTrackerState.warnings);
    if (areaFiles.empty()) {
        mapTrackerState.fatalErrors.push_back("No area files found in map pack.");
        mapTrackerState.fatalErrors.push_back("Expected folder/resource pattern: " +
                                              BuildMapTrackerResourcePath(mapTrackerState.resourcePathPrefix,
                                                                          std::string(CHECK_TRACKER_LOCATIONS_DIR) +
                                                                              "/*.json"));
        return false;
    }

    std::unordered_map<std::string, RandomizerCheck> checksByMapTrackerId =
        BuildGameCheckLookupByMapTrackerId(mapTrackerState.warnings);
    if (checksByMapTrackerId.empty()) {
        mapTrackerState.fatalErrors.push_back("No in-game checks were available for soh_id mapping.");
        return false;
    }
    outMappedMarkers = ParseMapMarkersFromPackAreas(areaFiles, checksByMapTrackerId, mapTrackerState.linkedChecks,
                                                    mapTrackerState.warnings, mapTrackerState.unresolvedLinks);

    std::vector<CheckDescriptor> descriptors = BuildVisibleCheckDescriptors();
    if (descriptors.empty()) {
        mapTrackerState.fatalErrors.push_back("No visible checks available to map. Load a randomizer save first.");
        return false;
    }
    for (const auto& descriptor : descriptors) {
        if (!mapTrackerState.linkedChecks.contains(descriptor.check)) {
            mapTrackerState.unassignedCheckIds.push_back(descriptor.check);
            mapTrackerState.unresolvedLinks.push_back(
                { "Unassigned in-game check: " + descriptor.checkDisplayName,
                  "No map marker with matching soh_id was found in the pack. Area: " +
                      RandomizerCheckObjects::GetRCAreaName(descriptor.area) });
        }
    }
    std::sort(mapTrackerState.unassignedCheckIds.begin(), mapTrackerState.unassignedCheckIds.end(),
              [](RandomizerCheck left, RandomizerCheck right) {
                  return static_cast<int>(left) < static_cast<int>(right);
              });
    mapTrackerState.unassignedCheckIds.erase(
        std::unique(mapTrackerState.unassignedCheckIds.begin(), mapTrackerState.unassignedCheckIds.end()),
        mapTrackerState.unassignedCheckIds.end());

    SPDLOG_INFO("[CheckTrackerMapDiag] soh_id mapping summary. mappedMarkers={} linkedChecks={} unresolved={} unassigned={}",
                outMappedMarkers.size(), mapTrackerState.linkedChecks.size(), mapTrackerState.unresolvedLinks.size(),
                mapTrackerState.unassignedCheckIds.size());
    return true;
}

static void BuildMapTabsFromMetadata(const std::filesystem::path& packFolderPath, const MapsMetadataParseResult& metadata) {
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
            if (!mapTrackerState.usingArchivePack) {
                tab.imageAbsolutePath = packFolderPath / tab.imageRelativePath;
            }
            tab.imageResourcePath = BuildMapTrackerResourcePath(mapTrackerState.resourcePathPrefix, tab.imageRelativePath);
            if (!mapTrackerState.usingArchivePack && !std::filesystem::exists(tab.imageAbsolutePath)) {
                tab.imageError = "Image file not found: " + tab.imageAbsolutePath.string();
            }
        } else {
            tab.imageError = "No image entry found in maps.json for map_id \"" + mapId + "\".";
        }

        mapTrackerState.tabIndexById[tab.mapId] = mapTrackerState.tabs.size();
        mapTrackerState.tabs.push_back(std::move(tab));
    }

    for (const auto& tab : mapTrackerState.tabs) {
        for (const auto& link : tab.links) {
            if (!mapTrackerState.tabIndexById.contains(link.targetMapId)) {
                mapTrackerState.warnings.push_back(
                    { "Missing target map for link",
                      "Map \"" + tab.mapName + "\" links to map_id \"" + link.targetMapId +
                          "\", but no tab with that id exists in maps.json." });
            }
        }
    }
}

static void BuildMapTabGroups() {
    bool hasNamedGroups = false;
    for (auto& tab : mapTrackerState.tabs) {
        tab.groupName = TrimCopy(tab.groupName);
        if (!tab.groupName.empty()) {
            hasNamedGroups = true;
        }
    }

    if (hasNamedGroups) {
        for (auto& tab : mapTrackerState.tabs) {
            if (tab.groupName.empty()) {
                tab.groupName = "Others";
            }
        }
    }

    for (size_t tabIndex = 0; tabIndex < mapTrackerState.tabs.size(); tabIndex++) {
        const std::string groupName = mapTrackerState.tabs[tabIndex].groupName;
        if (groupName.empty()) {
            continue;
        }
        if (!mapTrackerState.tabIndicesByGroup.contains(groupName)) {
            mapTrackerState.mapGroups.push_back(groupName);
        }
        mapTrackerState.tabIndicesByGroup[groupName].push_back(static_cast<int>(tabIndex));
    }
    for (const auto& [groupName, groupTabIndices] : mapTrackerState.tabIndicesByGroup) {
        if (!groupTabIndices.empty()) {
            mapTrackerState.lastSelectedTabByGroup[groupName] = groupTabIndices.front();
        }
    }
    if (!mapTrackerState.mapGroups.empty()) {
        mapTrackerState.selectedGroupName = mapTrackerState.mapGroups.front();
    } else {
        mapTrackerState.selectedGroupName.clear();
    }
}

static void LinkMapMarkersToTabs(const std::vector<MapMarker>& mappedMarkers) {
    for (const auto& marker : mappedMarkers) {
        if (!mapTrackerState.tabIndexById.contains(marker.mapId)) {
            mapTrackerState.unresolvedLinks.push_back(
                { "Missing map tab for linked marker",
                  "Could not find a tab for map_id \"" + marker.mapId + "\" while linking " +
                      GetCheckDisplayName(marker.check) + "." });
            continue;
        }
        int markerTabIndex = static_cast<int>(mapTrackerState.tabIndexById[marker.mapId]);
        mapTrackerState.tabs[static_cast<size_t>(markerTabIndex)].markers.push_back(marker);
    }
}

static bool LoadMapTabTextures(const std::filesystem::path& packFolderPath) {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (gui == nullptr) {
        mapTrackerState.fatalErrors.push_back("Could not access GUI texture loader.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: GUI texture loader was null.");
        return false;
    }

    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr ||
        context->GetResourceManager()->GetArchiveManager() == nullptr) {
        mapTrackerState.fatalErrors.push_back("Could not access archive manager for map texture resources.");
        SPDLOG_ERROR("[CheckTrackerMapDiag] Fatal: archive manager unavailable.");
        return false;
    }
    auto archiveManager = context->GetResourceManager()->GetArchiveManager();

    for (auto& tab : mapTrackerState.tabs) {
        std::sort(tab.markers.begin(), tab.markers.end(), [](const MapMarker& left, const MapMarker& right) {
            if (left.check == right.check) {
                return left.packCheckName < right.packCheckName;
            }
            return left.check < right.check;
        });

        if (!tab.imageError.empty()) {
            continue;
        }

        if (!mapTrackerState.usingArchivePack) {
            std::string imageValidationError;
            if (!ValidateMapImageFile(tab.imageAbsolutePath, imageValidationError)) {
                tab.imageError = imageValidationError;
                continue;
            }
        }

        if (!archiveManager->HasFile(tab.imageResourcePath)) {
            tab.imageError = "Image resource not indexed in archive: " + tab.imageResourcePath +
                             " | Archive mount root: " + mapTrackerState.assetsArchiveMountRoot.string();
            continue;
        }

        tab.textureName = "CHECK_TRACKER_MAP_" + tab.mapId;
        if (gui->HasTextureByName(tab.textureName)) {
            gui->UnloadTexture(tab.textureName);
        }

        try {
            gui->LoadTextureFromRawImage(tab.textureName, tab.imageResourcePath);
            tab.texture = gui->GetTextureByName(tab.textureName);
            tab.textureSize = gui->GetTextureSize(tab.textureName);
            tab.imageLoaded = tab.texture != 0 && tab.textureSize.x > 0.0f && tab.textureSize.y > 0.0f;
            if (!tab.imageLoaded) {
                tab.imageError = mapTrackerState.usingArchivePack
                                     ? "Failed to load map texture from resource path: " + tab.imageResourcePath
                                     : "Failed to load map texture from: " + tab.imageAbsolutePath.string() +
                                           " | Resource path: " + tab.imageResourcePath;
            }
        } catch (...) {
            tab.imageError = mapTrackerState.usingArchivePack
                                 ? "Failed to load map texture from resource path: " + tab.imageResourcePath
                                 : "Failed to load map texture from: " + tab.imageAbsolutePath.string() +
                                       " | Resource path: " + tab.imageResourcePath;
        }
    }
    return true;
}

static void FinalizeMapTrackerLoadSuccess() {
    mapTrackerState.loaded = true;
}

static void LogMapTrackerLoadSuccess(const std::chrono::steady_clock::time_point& loadStartTime) {
    SPDLOG_INFO("[CheckTrackerMapDiag] Load completed in {} ms. tabs={} warnings={} fatalErrors={}",
                GetElapsedMilliseconds(loadStartTime), mapTrackerState.tabs.size(), mapTrackerState.warnings.size(),
                mapTrackerState.fatalErrors.size());
}

void LoadMapTrackerData() {
    const auto loadStartTime = std::chrono::steady_clock::now();
    InitializeMapTrackerLoadState();
    const std::filesystem::path packFolderPath = mapTrackerState.assetsRoot;

    SPDLOG_INFO("[CheckTrackerMapDiag] Load start. assets='{}' candidates='{}'", mapTrackerState.assetsRoot.string(),
                BuildMapTrackerAssetsRootCandidatesSummary());

    if (!EnsureMapPackArchiveMounted(packFolderPath)) {
        return;
    }

    json mapsJson;
    std::filesystem::path mapsDiskPath;
    std::string mapsResourcePath;
    if (!LoadMapTrackerMetadataJson(packFolderPath, mapsJson, mapsDiskPath, mapsResourcePath)) {
        return;
    }

    MapsMetadataParseResult metadata;
    if (!ParseMapMetadataEntries(mapsJson, mapsResourcePath, mapsDiskPath, metadata)) {
        return;
    }

    std::vector<MapMarker> mappedMarkers;
    if (!BuildMapMarkersAndCheckLinks(packFolderPath, mappedMarkers)) {
        return;
    }

    BuildMapTabsFromMetadata(packFolderPath, metadata);
    BuildMapTabGroups();
    LinkMapMarkersToTabs(mappedMarkers);

    if (!LoadMapTabTextures(packFolderPath)) {
        return;
    }

    FinalizeMapTrackerLoadSuccess();
    LogMapTrackerLoadSuccess(loadStartTime);
}

} // namespace CheckTracker

