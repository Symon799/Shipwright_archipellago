#pragma once

#include "randomizer_check_tracker.h"
#include "randomizer_check_logic.h"
#include "soh/cvar_prefixes.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CheckTracker {

inline constexpr const char* CHECK_TRACKER_MAP_MODE_CVAR = CVAR_TRACKER_CHECK("MapMode");
inline constexpr const char* CHECK_TRACKER_MAP_DEBUG_CVAR = CVAR_TRACKER_CHECK("MapDebugInfo");
inline constexpr const char* CHECK_TRACKER_MAP_ASSETS_ROOT = "mods/check_tracker_map_pack";
inline constexpr const char* CHECK_TRACKER_MAPS_JSON = "maps.json";
inline constexpr const char* CHECK_TRACKER_LOCATIONS_DIR = "areas";
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_DONE = IM_COL32(130, 130, 130, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_AVAILABLE = IM_COL32(55, 185, 85, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_AGE_MISMATCH = IM_COL32(255, 219, 73, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_TAB_COLOR_AGE_MISMATCH = IM_COL32(196, 156, 36, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_UNAVAILABLE = IM_COL32(200, 65, 65, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_BORDER = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_LINK_BORDER_AGE_MISMATCH = IM_COL32(230, 188, 19, 255);
inline constexpr ImU32 CHECK_TRACKER_MAP_COLOR_LINK_BORDER_UNAVAILABLE = IM_COL32(125, 28, 28, 255);
inline constexpr float CHECK_TRACKER_MAP_MIN_MARKER_PIXEL_SIZE = 20.0f;
inline constexpr float CHECK_TRACKER_MAP_MULTI_MARKER_SIZE_SCALE = 1.14f;
inline constexpr float CHECK_TRACKER_MAP_TOOLTIP_MAX_VIEWPORT_HEIGHT_RATIO = 0.9f;
inline constexpr float CHECK_TRACKER_MAP_ZOOM_MIN = 1.0f;
inline constexpr float CHECK_TRACKER_MAP_ZOOM_MAX = 5.0f;
inline constexpr float CHECK_TRACKER_MAP_ZOOM_WHEEL_STEP = 1.15f;
inline constexpr float CHECK_TRACKER_MAP_BORDER_THICKNESS = 1.5f;
inline constexpr float CHECK_TRACKER_MAP_LINK_UNAVAILABLE_BORDER_THICKNESS =
    CHECK_TRACKER_MAP_BORDER_THICKNESS * 2.0f;

struct MapPlacement {
    std::string mapId;
    float x = 0.0f;
    float y = 0.0f;
    float size = 22.0f;
};

struct CheckDescriptor {
    RandomizerCheck check = RC_UNKNOWN_CHECK;
    RandomizerCheckArea area = RCAREA_INVALID;
    std::string checkDisplayName;
};

struct MapMarker {
    RandomizerCheck check = RC_UNKNOWN_CHECK;
    std::string mapId;
    std::string packCheckName;
    float x = 0.0f;
    float y = 0.0f;
    float size = 22.0f;
};

struct MapLink {
    std::string targetMapId;
    int16_t entranceIndex = -1;
    bool disableEntranceLogic = false;
    float x = 0.0f;
    float y = 0.0f;
    float size = 22.0f;
};

struct MapTabData {
    std::string mapId;
    std::string mapName;
    std::string groupName;
    std::string imageRelativePath;
    std::string imageResourcePath;
    std::string textureName;
    ImTextureID texture = 0;
    ImVec2 textureSize = { 0.0f, 0.0f };
    bool imageLoaded = false;
    float zoomFactor = 1.0f;
    ImVec2 panOffset = { 0.0f, 0.0f };
    std::string imageError;
    std::vector<MapMarker> markers;
    std::vector<MapLink> links;
};

struct MapIssueEntry {
    std::string summary;
    std::string details;
};

inline std::string FormatMapIssueLine(const MapIssueEntry& issue) {
    if (issue.details.empty()) {
        return issue.summary;
    }
    if (issue.summary.empty()) {
        return issue.details;
    }
    return issue.summary + " | " + issue.details;
}

struct MapPackAreaFileRef {
    std::string resourcePath;
    std::string displayName;
};

struct MapTrackerState {
    bool attemptedLoad = false;
    bool loaded = false;
    std::filesystem::path assetsRoot;
    std::filesystem::path assetsArchiveMountRoot;
    std::filesystem::path mountedArchivePath;
    std::string resourcePathPrefix;
    std::vector<std::string> fatalErrors;
    std::vector<MapIssueEntry> warnings;
    std::vector<MapIssueEntry> unresolvedLinks;
    std::vector<RandomizerCheck> unassignedCheckIds;
    std::vector<MapIssueEntry> archipelagoScoutedWithoutMapId;
    std::vector<MapTabData> tabs;
    std::unordered_map<std::string, size_t> tabIndexById;
    std::vector<std::string> mapGroups;
    std::unordered_map<std::string, std::vector<int>> tabIndicesByGroup;
    std::string selectedGroupName;
    std::unordered_map<std::string, int> lastSelectedTabByGroup;
    std::unordered_set<RandomizerCheck> linkedChecks;
    std::unordered_map<RandomizerCheck, std::string> checkHints;
    std::unordered_set<RandomizerCheck> revealedCheckHints;
    std::string requestedTabId;
    int selectedTabIndex = 0;
    int lastMapViewTabIndex = -1;
    RandomizerCheckArea lastFocusedArea = RCAREA_INVALID;
    SceneID lastFocusedScene = SCENE_ID_MAX;
};

struct MapLinkBorderStyle {
    ImU32 color = CHECK_TRACKER_MAP_COLOR_BORDER;
    float thickness = CHECK_TRACKER_MAP_BORDER_THICKNESS;
};

extern bool enableAvailableChecks;
extern bool onlyShowAvailable;
extern bool showLogicTooltip;
extern bool showMapDebugDetails;
extern RandomizerCheckArea previousArea;
extern RandomizerCheckArea currentArea;
extern std::map<RandomizerCheckArea, std::vector<RandomizerCheck>> checksByArea;

extern MapTrackerState mapTrackerState;

bool IsMapModeEnabled();
void SetMapModeEnabled(bool enabled);
std::string GetMapTrackerAssetsRootAbsoluteString();
void ResetMapTrackerState(bool unloadTextures);
void LoadMapTrackerData();
void DrawMapTrackerContent();
void UpdateRequestedMapTabFromCurrentArea(bool force);
void InvalidateMapTrackerRenderCache(bool closePopups = true);

std::string TrimCopy(const std::string& value);
std::string GetGameCheckMapTrackerId(RandomizerCheck rc);

bool IsCheckHidden(RandomizerCheck rc);
bool CanToggleSkippedStateForCheck(RandomizerCheck rc);
bool ToggleSkippedStateForCheck(RandomizerCheck rc);
std::string GetCheckDisplayName(RandomizerCheck rc);
std::string GetCheckExtraInfoText(RandomizerCheck rc);
Color_RGBA8 GetLegacyCheckExtraColor(RandomizerCheck rc);
std::optional<std::string> ResolvePreferredMapTabIdForArea(RandomizerCheckArea area);
std::optional<CheckAgeTimeAvailabilityInfo> EvaluateMapLinkAgeTimeAvailability(const MapLink& link);
std::optional<std::string> GetMapLinkRequirementSummary(const MapLink& link);
MapLinkBorderStyle GetMapLinkBorderStyle(const std::optional<CheckAgeTimeAvailabilityInfo>& availabilityInfo);

} // namespace CheckTracker
