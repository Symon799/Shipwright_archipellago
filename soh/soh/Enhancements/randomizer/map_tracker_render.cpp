#include "check_tracker_visibility_debug.h"
#include "map_tracker_internal.h"
#include "soh/OTRGlobals.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/util.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace CheckTracker {
using namespace UIWidgets;

bool IsCheckDoneForMapDisplay(RandomizerCheck rc) {
    auto* itemLocation = OTRGlobals::Instance->gRandoContext->GetItemLocation(rc);
    if (itemLocation->GetIsSkipped() || itemLocation->HasObtained()) {
        return true;
    }

    RandomizerCheckStatus status = itemLocation->GetCheckStatus();
    return status == RCSHOW_COLLECTED || status == RCSHOW_SAVED;
}

struct MapTabVisualSummary {
    bool hasVisibleChecks = false;
    bool hasAvailableChecks = false;
    bool hasRequirementMismatchChecks = false;
    bool hasUnavailableChecks = false;
    bool hasDoneChecks = false;
};

MapTabVisualSummary BuildMapTabVisualSummary(const MapTabData& tab, bool mqSpoilers) {
    MapTabVisualSummary summary;

    for (const auto& marker : tab.markers) {
        if (!IsVisibleInCheckTracker(marker.check) || IsCheckHidden(marker.check)) {
            continue;
        }

        auto* location = Rando::StaticData::GetLocation(marker.check);
        if (location == nullptr || !(IsAreaSpoiled(location->GetArea()) || mqSpoilers)) {
            continue;
        }

        auto* itemLocation = OTRGlobals::Instance->gRandoContext->GetItemLocation(marker.check);
        if (itemLocation == nullptr) {
            continue;
        }

        summary.hasVisibleChecks = true;

        bool isDone = IsCheckDoneForMapDisplay(marker.check);
        bool isAvailable = itemLocation->IsAvailable();
        bool isRequirementMismatch = isAvailable && !isDone && IsCheckAvailableButWrongAgeOrTime(marker.check);

        if (isDone) {
            summary.hasDoneChecks = true;
        } else if (isRequirementMismatch) {
            summary.hasRequirementMismatchChecks = true;
        } else if (isAvailable) {
            summary.hasAvailableChecks = true;
        } else {
            summary.hasUnavailableChecks = true;
        }
    }

    return summary;
}

ImVec4 GetMapTabBaseColor(const MapTabVisualSummary& summary) {
    if (!summary.hasVisibleChecks) {
        return ImVec4(0.38f, 0.38f, 0.38f, 0.95f);
    }
    if (summary.hasAvailableChecks) {
        return ImGui::ColorConvertU32ToFloat4(CHECK_TRACKER_MAP_COLOR_AVAILABLE);
    }
    if (summary.hasRequirementMismatchChecks) {
        return ImGui::ColorConvertU32ToFloat4(CHECK_TRACKER_MAP_TAB_COLOR_AGE_MISMATCH);
    }
    if (summary.hasUnavailableChecks) {
        return ImGui::ColorConvertU32ToFloat4(CHECK_TRACKER_MAP_COLOR_UNAVAILABLE);
    }
    if (summary.hasDoneChecks) {
        return ImGui::ColorConvertU32ToFloat4(CHECK_TRACKER_MAP_COLOR_DONE);
    }
    return ImVec4(0.38f, 0.38f, 0.38f, 0.95f);
}

ImVec4 ScaleMapTabColor(const ImVec4& color, float scale) {
    return ImVec4(std::clamp(color.x * scale, 0.0f, 1.0f), std::clamp(color.y * scale, 0.0f, 1.0f),
                  std::clamp(color.z * scale, 0.0f, 1.0f), color.w);
}

void DrawMapTrackerIssuesTab() {
    DrawCheckTrackerVisibilityDebugControls();
    ImGui::Separator();

    auto drawIssueCategory = [](const char* categoryName, const std::vector<MapIssueEntry>& issues, const char* emptyText,
                                const ImVec4& color) {
        std::string headerLabel = fmt::format("{} ({})", categoryName, issues.size());
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        bool open = ImGui::CollapsingHeader(headerLabel.c_str());
        ImGui::PopStyleColor();

        if (!open) {
            return;
        }

        if (issues.empty()) {
            ImGui::TextDisabled("%s", emptyText);
            return;
        }

        for (size_t issueIndex = 0; issueIndex < issues.size(); issueIndex++) {
            const auto& issue = issues[issueIndex];
            ImGui::PushID(static_cast<int>(issueIndex));
            ImGui::TextUnformatted(FormatMapIssueLine(issue).c_str());
            ImGui::PopID();
        }
    };

    drawIssueCategory("Warnings", mapTrackerState.warnings, "No warnings.", ImVec4(1.0f, 0.85f, 0.45f, 1.0f));
    drawIssueCategory("Unlinked checks", mapTrackerState.unresolvedLinks, "No unlinked checks.",
                      ImVec4(1.0f, 0.5f, 0.5f, 1.0f));

    std::string unassignedHeader =
        fmt::format("Unassigned in-game map tracker ids ({})", mapTrackerState.unassignedCheckIds.size());
    if (ImGui::CollapsingHeader(unassignedHeader.c_str())) {
        if (mapTrackerState.unassignedCheckIds.empty()) {
            ImGui::TextDisabled("No unassigned checks.");
        } else {
            for (RandomizerCheck rc : mapTrackerState.unassignedCheckIds) {
                Rando::Location* location = Rando::StaticData::GetLocation(rc);
                const std::string sohId = GetGameCheckMapTrackerId(rc);
                const std::string line =
                    fmt::format("{} | {} | soh_id: {}", GetCheckDisplayName(rc),
                                RandomizerCheckObjects::GetRCAreaName(location->GetArea()),
                                sohId.empty() ? "(missing)" : sohId);
                ImGui::TextUnformatted(line.c_str());
            }
        }
    }

    drawIssueCategory("Scouted checks without map pack id (AP)", mapTrackerState.archipelagoScoutedWithoutMapId,
                      "No scouted checks missing an in-game soh_id.", ImVec4(0.75f, 0.55f, 1.0f, 1.0f));

    if (mapTrackerState.warnings.empty() && mapTrackerState.unresolvedLinks.empty() &&
        mapTrackerState.unassignedCheckIds.empty() && mapTrackerState.archipelagoScoutedWithoutMapId.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("No issues found.");
    }
}

struct RenderableMapMarker {
    const MapMarker* marker = nullptr;
    bool isDone = false;
    bool isAvailable = false;
    bool isRequirementMismatch = false;
    ImU32 fillColor = CHECK_TRACKER_MAP_COLOR_UNAVAILABLE;
};

struct CachedMapTabRenderData {
    std::vector<RenderableMapMarker> renderableMarkers;
    std::vector<std::string> stackOrder;
    std::unordered_map<std::string, std::vector<RenderableMapMarker>> renderableMarkersByStackKey;
    std::vector<std::optional<CheckAgeTimeAvailabilityInfo>> linkAvailabilityByIndex;
    std::vector<std::optional<std::string>> linkRequirementSummariesByIndex;
};

struct MapTrackerRenderCache {
    uint64_t generation = 1;
    uint64_t cachedGeneration = 0;
    bool valid = false;
    bool mqSpoilers = false;
    bool isAdult = false;
    bool isNight = false;
    std::vector<MapTabVisualSummary> tabVisualSummaries;
    std::vector<CachedMapTabRenderData> tabRenderDataByIndex;
};

static MapTrackerRenderCache mapTrackerRenderCache;

struct ClusterPopupState {
    bool open = false;
    std::string tabId;
    std::string stackKey;
    ImVec2 popupPosition = { 0.0f, 0.0f };
    ImVec2 anchorMin = { 0.0f, 0.0f };
    ImVec2 anchorMax = { 0.0f, 0.0f };
    ImVec2 lastWindowPosition = { 0.0f, 0.0f };
    ImVec2 lastWindowSize = { 0.0f, 0.0f };
    bool hasLastWindowRect = false;
};

static ClusterPopupState mapClusterPopupState;

struct LinkPopupState {
    bool open = false;
    std::string tabId;
    std::string targetTabId;
    ImVec2 popupPosition = { 0.0f, 0.0f };
    ImVec2 anchorMin = { 0.0f, 0.0f };
    ImVec2 anchorMax = { 0.0f, 0.0f };
    ImVec2 lastWindowPosition = { 0.0f, 0.0f };
    ImVec2 lastWindowSize = { 0.0f, 0.0f };
    bool hasLastWindowRect = false;
};

static LinkPopupState mapLinkPopupState;

static void CloseClusterPopup(bool clearTabId = false) {
    mapClusterPopupState.open = false;
    if (clearTabId) {
        mapClusterPopupState.tabId.clear();
    }
    mapClusterPopupState.stackKey.clear();
    mapClusterPopupState.anchorMin = { 0.0f, 0.0f };
    mapClusterPopupState.anchorMax = { 0.0f, 0.0f };
    mapClusterPopupState.hasLastWindowRect = false;
}

static void CloseLinkPopup(bool clearTabId = false) {
    mapLinkPopupState.open = false;
    if (clearTabId) {
        mapLinkPopupState.tabId.clear();
    }
    mapLinkPopupState.targetTabId.clear();
    mapLinkPopupState.anchorMin = { 0.0f, 0.0f };
    mapLinkPopupState.anchorMax = { 0.0f, 0.0f };
    mapLinkPopupState.hasLastWindowRect = false;
}

static std::string GetMapTrackerCheckHint(RandomizerCheck check) {
    auto hintIt = mapTrackerState.checkHints.find(check);
    if (hintIt == mapTrackerState.checkHints.end()) {
        return "";
    }

    return hintIt->second;
}

static bool ToggleMapTrackerCheckHint(RandomizerCheck check) {
    if (GetMapTrackerCheckHint(check).empty()) {
        return false;
    }

    if (mapTrackerState.revealedCheckHints.contains(check)) {
        mapTrackerState.revealedCheckHints.erase(check);
    } else {
        mapTrackerState.revealedCheckHints.insert(check);
    }

    return true;
}

struct MarkerTooltipContent {
    bool showTitle = true;
    std::string checkName;
    std::string requirementSummary;
    std::string extraText;
    Color_RGBA8 extraColor = { 255, 255, 255, 255 };
    std::string hintText;
    bool showHintPrompt = false;
    std::vector<std::string> logicBranches;
    std::string checkMapTrackerId;
    std::string packCheckName;
};

struct MarkerTooltipLayout {
    ImVec2 estimatedWindowSize = { 0.0f, 0.0f };
    ImVec2 maxWindowSize = { 0.0f, 0.0f };
    float contentWidth = 0.0f;
};

enum class TooltipPlacementMode {
    Right,
    Left,
    Below,
    Above,
};

struct TooltipPlacement {
    TooltipPlacementMode mode = TooltipPlacementMode::Right;
    float windowWidth = 0.0f;
};

struct PopupWindowLayout {
    ImVec2 windowSize = { 0.0f, 0.0f };
    float childHeight = 0.0f;
};

struct PopupRowLayout {
    float rowHeight = 0.0f;
    float statusSize = 0.0f;
    float statusSpacing = 0.0f;
    float textAvailableWidth = 0.0f;
    float checkNameWrapWidth = 0.0f;
    bool extraOnSameLine = false;
};

struct PendingMarkerTooltip {
    bool active = false;
    MarkerTooltipContent content;
    ImVec2 anchorMin = { 0.0f, 0.0f };
    ImVec2 anchorMax = { 0.0f, 0.0f };
    const ImGuiViewport* viewport = nullptr;
    bool hasAvoidRect = false;
    ImVec2 avoidRectMin = { 0.0f, 0.0f };
    ImVec2 avoidRectMax = { 0.0f, 0.0f };
};

static PendingMarkerTooltip pendingMarkerTooltip;

static float GetCheckTrackerFontScale() {
    return std::max(0.1f, CVarGetFloat(CVAR_TRACKER_CHECK("FontSize"), 1.0f));
}

static void ApplyCheckTrackerFontScaleToCurrentWindow() {
    ImGui::SetWindowFontScale(GetCheckTrackerFontScale());
}

static ImVec2 CalcCheckTrackerTextSize(const std::string& text, float wrapWidth = 0.0f) {
    if (text.empty()) {
        return ImVec2(0.0f, 0.0f);
    }

    ImFont* font = ImGui::GetFont();
    const float resolvedWrapWidth = wrapWidth > 0.0f ? wrapWidth : 0.0f;
    if (font == nullptr) {
        return ImGui::CalcTextSize(text.c_str(), nullptr, false, resolvedWrapWidth > 0.0f ? resolvedWrapWidth : -1.0f);
    }

    const float fontSize = font->FontSize * GetCheckTrackerFontScale();
    return font->CalcTextSizeA(fontSize, FLT_MAX, resolvedWrapWidth, text.c_str(), nullptr, nullptr);
}

static float GetCheckTrackerTextLineHeight() {
    return std::max(1.0f, CalcCheckTrackerTextSize("Ag").y);
}

static float GetPopupContentWidthFromWindowWidth(float windowWidth) {
    return std::max(1.0f, windowWidth - (ImGui::GetStyle().WindowPadding.x * 2.0f) - 4.0f);
}

static std::string BuildPopupExtraLabel(const std::string& extraText) {
    return extraText.empty() ? std::string() : fmt::format("({})", extraText);
}

static ImVec2 CalcPopupTextSize(const std::string& text, float wrapWidth = 0.0f) {
    if (text.empty()) {
        return ImVec2(0.0f, 0.0f);
    }

    return ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth > 0.0f ? wrapWidth : -1.0f);
}

static PopupRowLayout ComputePopupRowLayout(const std::string& checkName, const std::string& extraText,
                                            float rowAvailableWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineHeight = ImGui::GetTextLineHeight();

    PopupRowLayout layout;
    layout.statusSize = std::max(10.0f, lineHeight - 2.0f);
    layout.statusSpacing = std::max(6.0f, style.ItemInnerSpacing.x);
    layout.textAvailableWidth = std::max(24.0f, rowAvailableWidth - layout.statusSize - layout.statusSpacing);
    layout.checkNameWrapWidth = layout.textAvailableWidth;

    const float checkNameSingleLineWidth = CalcPopupTextSize(checkName).x;
    const float checkNameHeight = std::max(lineHeight, CalcPopupTextSize(checkName, layout.textAvailableWidth).y);

    float textHeight = checkNameHeight;
    std::string extraLabel = BuildPopupExtraLabel(extraText);
    if (!extraLabel.empty()) {
        const float extraLabelWidth = CalcPopupTextSize(extraLabel).x;
        const bool canFitInline =
            (checkNameSingleLineWidth + style.ItemSpacing.x + extraLabelWidth) <= layout.textAvailableWidth;
        if (canFitInline) {
            layout.extraOnSameLine = true;
            layout.checkNameWrapWidth =
                std::max(1.0f, layout.textAvailableWidth - style.ItemSpacing.x - extraLabelWidth);
        } else {
            const float extraHeight = std::max(lineHeight, CalcPopupTextSize(extraLabel, layout.textAvailableWidth).y);
            textHeight += style.ItemSpacing.y + extraHeight;
        }
    }

    layout.rowHeight = std::max(layout.statusSize, textHeight);
    return layout;
}

static float ComputePopupHeaderDesiredContentWidth(const std::optional<int>& popupNavigationTargetTabIndex,
                                                   const std::optional<std::string>& requirementSummary) {
    if (!popupNavigationTargetTabIndex.has_value() || *popupNavigationTargetTabIndex < 0 ||
        *popupNavigationTargetTabIndex >= static_cast<int>(mapTrackerState.tabs.size())) {
        return 0.0f;
    }

    const MapTabData& popupTargetTab = mapTrackerState.tabs[static_cast<size_t>(*popupNavigationTargetTabIndex)];
    float desiredContentWidth = CalcPopupTextSize(popupTargetTab.mapName).x + 1.0f;
    if (requirementSummary.has_value() && !requirementSummary->empty()) {
        desiredContentWidth = std::max(desiredContentWidth, CalcPopupTextSize(BuildPopupExtraLabel(*requirementSummary)).x);
    }

    return desiredContentWidth;
}

static float ComputePopupHeaderHeight(const std::optional<int>& popupNavigationTargetTabIndex,
                                      const std::optional<std::string>& requirementSummary, float contentWidth) {
    if (!popupNavigationTargetTabIndex.has_value() || *popupNavigationTargetTabIndex < 0 ||
        *popupNavigationTargetTabIndex >= static_cast<int>(mapTrackerState.tabs.size())) {
        return 0.0f;
    }

    const ImGuiStyle& popupStyle = ImGui::GetStyle();
    const float lineHeight = ImGui::GetTextLineHeight();
    const MapTabData& popupTargetTab = mapTrackerState.tabs[static_cast<size_t>(*popupNavigationTargetTabIndex)];

    float headerHeight = std::max(lineHeight, CalcPopupTextSize(popupTargetTab.mapName, contentWidth).y);
    if (requirementSummary.has_value() && !requirementSummary->empty()) {
        const std::string summaryLabel = BuildPopupExtraLabel(*requirementSummary);
        const float summaryHeight = std::max(lineHeight, CalcPopupTextSize(summaryLabel, contentWidth).y);
        headerHeight += popupStyle.ItemSpacing.y + summaryHeight;
    }

    headerHeight += (popupStyle.ItemSpacing.y * 2.0f) + 2.0f;
    return headerHeight;
}

static bool ShouldRenderMapMarker(const MapMarker& marker, bool mqSpoilers) {
    if (!IsVisibleInCheckTracker(marker.check)) {
        return false;
    }

    auto* itemLocation = OTRGlobals::Instance->gRandoContext->GetItemLocation(marker.check);
    if (!IsMapModeEnabled() && enableAvailableChecks && onlyShowAvailable && !itemLocation->IsAvailable()) {
        return false;
    }

    if (IsCheckHidden(marker.check)) {
        return false;
    }

    auto* location = Rando::StaticData::GetLocation(marker.check);
    return IsAreaSpoiled(location->GetArea()) || mqSpoilers;
}

static std::string BuildMapMarkerStackKey(const MapMarker& marker) {
    int xQuantized = static_cast<int>(std::lround(marker.x * 100.0f));
    int yQuantized = static_cast<int>(std::lround(marker.y * 100.0f));
    return fmt::format("{}:{}", xQuantized, yQuantized);
}

static RenderableMapMarker CreateRenderableMapMarker(const MapMarker& marker) {
    auto* itemLocation = OTRGlobals::Instance->gRandoContext->GetItemLocation(marker.check);
    bool isDone = IsCheckDoneForMapDisplay(marker.check);
    bool isAvailable = itemLocation->IsAvailable();
    bool isRequirementMismatch = isAvailable && !isDone && IsCheckAvailableButWrongAgeOrTime(marker.check);

    ImU32 fillColor = CHECK_TRACKER_MAP_COLOR_UNAVAILABLE;
    if (isDone) {
        fillColor = CHECK_TRACKER_MAP_COLOR_DONE;
    } else if (isRequirementMismatch) {
        fillColor = CHECK_TRACKER_MAP_COLOR_AGE_MISMATCH;
    } else if (isAvailable) {
        fillColor = CHECK_TRACKER_MAP_COLOR_AVAILABLE;
    }

    return { &marker, isDone, isAvailable, isRequirementMismatch, fillColor };
}

static void BuildRenderableMarkerClusters(
    const std::vector<RenderableMapMarker>& renderableMarkers, std::vector<std::string>& outStackOrder,
    std::unordered_map<std::string, std::vector<RenderableMapMarker>>& outRenderableMarkersByStackKey) {
    outStackOrder.clear();
    outRenderableMarkersByStackKey.clear();

    for (const auto& renderableMarker : renderableMarkers) {
        std::string stackKey = BuildMapMarkerStackKey(*renderableMarker.marker);
        if (!outRenderableMarkersByStackKey.contains(stackKey)) {
            outStackOrder.push_back(stackKey);
        }
        outRenderableMarkersByStackKey[stackKey].push_back(renderableMarker);
    }
}

static MarkerTooltipContent BuildMarkerTooltipContent(RandomizerCheck check, const std::string& packCheckName) {
    MarkerTooltipContent content;
    content.checkName = GetCheckDisplayName(check);
    content.requirementSummary = GetCheckRequirementSummary(check);
    content.extraText = GetCheckExtraInfoText(check);
    if (!content.extraText.empty()) {
        content.extraColor = GetLegacyCheckExtraColor(check);
    }
    content.hintText = GetMapTrackerCheckHint(check);
    if (!content.hintText.empty() && !mapTrackerState.revealedCheckHints.contains(check)) {
        content.hintText.clear();
        content.showHintPrompt = true;
    }
    if (showLogicTooltip) {
        content.logicBranches = GetCheckLogicBranches(check);
    }
    if (showMapDebugDetails) {
        content.checkMapTrackerId = GetGameCheckMapTrackerId(check);
        content.packCheckName = packCheckName;
    }
    return content;
}

static MarkerTooltipContent BuildPopupRowTooltipContent(RandomizerCheck check) {
    MarkerTooltipContent content;
    content.showTitle = false;
    content.requirementSummary = GetCheckRequirementSummary(check);
    content.hintText = GetMapTrackerCheckHint(check);
    if (!content.hintText.empty() && !mapTrackerState.revealedCheckHints.contains(check)) {
        content.hintText.clear();
        content.showHintPrompt = true;
    }
    if (showLogicTooltip) {
        content.logicBranches = GetCheckLogicBranches(check);
    }
    return content;
}

static bool HasMarkerTooltipVisibleContent(const MarkerTooltipContent& content) {
    return (content.showTitle && !content.checkName.empty()) || !content.requirementSummary.empty() ||
           !content.extraText.empty() || !content.hintText.empty() || content.showHintPrompt ||
           !content.logicBranches.empty() || !content.checkMapTrackerId.empty() || !content.packCheckName.empty();
}

static std::vector<RenderableMapMarker> BuildRenderableMarkersForTab(const MapTabData& tab, bool mqSpoilers) {
    std::vector<RenderableMapMarker> renderableMarkers;
    renderableMarkers.reserve(tab.markers.size());

    for (const auto& marker : tab.markers) {
        if (!ShouldRenderMapMarker(marker, mqSpoilers)) {
            continue;
        }
        renderableMarkers.push_back(CreateRenderableMapMarker(marker));
    }

    return renderableMarkers;
}

static CachedMapTabRenderData BuildCachedMapTabRenderData(const MapTabData& tab, bool mqSpoilers) {
    CachedMapTabRenderData cachedTabRenderData;
    cachedTabRenderData.renderableMarkers = BuildRenderableMarkersForTab(tab, mqSpoilers);
    BuildRenderableMarkerClusters(cachedTabRenderData.renderableMarkers, cachedTabRenderData.stackOrder,
                                  cachedTabRenderData.renderableMarkersByStackKey);

    cachedTabRenderData.linkAvailabilityByIndex.reserve(tab.links.size());
    cachedTabRenderData.linkRequirementSummariesByIndex.reserve(tab.links.size());
    for (const auto& link : tab.links) {
        cachedTabRenderData.linkAvailabilityByIndex.push_back(EvaluateMapLinkAgeTimeAvailability(link));
        cachedTabRenderData.linkRequirementSummariesByIndex.push_back(GetMapLinkRequirementSummary(link));
    }

    return cachedTabRenderData;
}

void InvalidateMapTrackerRenderCache(bool closePopups) {
    mapTrackerRenderCache.valid = false;
    mapTrackerRenderCache.cachedGeneration = 0;
    mapTrackerRenderCache.generation++;
    if (closePopups) {
        CloseClusterPopup();
        CloseLinkPopup();
    }
}

static bool IsMapTrackerRenderCacheCurrent(bool mqSpoilers) {
    return mapTrackerRenderCache.valid &&
           mapTrackerRenderCache.cachedGeneration == mapTrackerRenderCache.generation &&
           mapTrackerRenderCache.mqSpoilers == mqSpoilers &&
           mapTrackerRenderCache.isAdult == static_cast<bool>(LINK_IS_ADULT) &&
           mapTrackerRenderCache.isNight == static_cast<bool>(IS_NIGHT) &&
           mapTrackerRenderCache.tabVisualSummaries.size() == mapTrackerState.tabs.size() &&
           mapTrackerRenderCache.tabRenderDataByIndex.size() == mapTrackerState.tabs.size();
}

static void RebuildMapTrackerRenderCache(bool mqSpoilers) {
    mapTrackerRenderCache.tabVisualSummaries.resize(mapTrackerState.tabs.size());
    mapTrackerRenderCache.tabRenderDataByIndex.resize(mapTrackerState.tabs.size());

    for (size_t tabIndex = 0; tabIndex < mapTrackerState.tabs.size(); tabIndex++) {
        const MapTabData& tab = mapTrackerState.tabs[tabIndex];
        mapTrackerRenderCache.tabVisualSummaries[tabIndex] = BuildMapTabVisualSummary(tab, mqSpoilers);
        mapTrackerRenderCache.tabRenderDataByIndex[tabIndex] = BuildCachedMapTabRenderData(tab, mqSpoilers);
    }

    mapTrackerRenderCache.cachedGeneration = mapTrackerRenderCache.generation;
    mapTrackerRenderCache.mqSpoilers = mqSpoilers;
    mapTrackerRenderCache.isAdult = static_cast<bool>(LINK_IS_ADULT);
    mapTrackerRenderCache.isNight = static_cast<bool>(IS_NIGHT);
    mapTrackerRenderCache.valid = true;
}

static const MapTrackerRenderCache& GetMapTrackerRenderCache(bool mqSpoilers) {
    if (!IsMapTrackerRenderCacheCurrent(mqSpoilers)) {
        RebuildMapTrackerRenderCache(mqSpoilers);
    }

    return mapTrackerRenderCache;
}

static ImVec2 ClampWindowPositionToViewport(const ImVec2& desiredPosition, const ImVec2& windowSize,
                                            const ImGuiViewport* viewport) {
    if (viewport == nullptr) {
        return desiredPosition;
    }

    constexpr float viewportPadding = 8.0f;
    const float minX = viewport->WorkPos.x + viewportPadding;
    const float minY = viewport->WorkPos.y + viewportPadding;
    const float maxX = std::max(minX, (viewport->WorkPos.x + viewport->WorkSize.x) - windowSize.x - viewportPadding);
    const float maxY = std::max(minY, (viewport->WorkPos.y + viewport->WorkSize.y) - windowSize.y - viewportPadding);

    return ImVec2(std::clamp(desiredPosition.x, minX, maxX), std::clamp(desiredPosition.y, minY, maxY));
}

static ImVec2 ComputeAnchoredPopupPosition(const ImVec2& anchorMin, const ImVec2& anchorMax, const ImVec2& windowSize,
                                          const ImGuiViewport* viewport) {
    constexpr float popupOffsetX = 10.0f;
    constexpr float popupOffsetY = 4.0f;

    if (viewport == nullptr) {
        return ImVec2(anchorMax.x + popupOffsetX, anchorMin.y - popupOffsetY);
    }

    constexpr float viewportPadding = 8.0f;
    const float viewportMinX = viewport->WorkPos.x + viewportPadding;
    const float viewportMaxX = viewport->WorkPos.x + viewport->WorkSize.x - viewportPadding;

    const float preferredRightX = anchorMax.x + popupOffsetX;
    const float preferredLeftX = anchorMin.x - windowSize.x - popupOffsetX;

    float popupX = preferredRightX;
    if ((preferredRightX + windowSize.x) > viewportMaxX && preferredLeftX >= viewportMinX) {
        popupX = preferredLeftX;
    }

    const ImVec2 desiredPosition(popupX, anchorMin.y - popupOffsetY);
    return ClampWindowPositionToViewport(desiredPosition, windowSize, viewport);
}

static float ComputeTooltipDesiredContentWidth(const MarkerTooltipContent& content) {
    float contentWidth = 0.0f;
    auto accumulateWidth = [&](const std::string& text) {
        if (!text.empty()) {
            contentWidth = std::max(contentWidth, CalcCheckTrackerTextSize(text).x);
        }
    };

    if (content.showTitle) {
        accumulateWidth(content.checkName);
    }
    accumulateWidth(content.requirementSummary);
    if (!content.extraText.empty()) {
        accumulateWidth(fmt::format("({})", content.extraText));
    }
    if (!content.hintText.empty()) {
        accumulateWidth(fmt::format("Hint: {}", content.hintText));
    } else if (content.showHintPrompt) {
        accumulateWidth("Right click to show hint.");
    }
    for (size_t branchIndex = 0; branchIndex < content.logicBranches.size(); branchIndex++) {
        accumulateWidth(branchIndex == 0 ? fmt::format("Logic: {}", content.logicBranches[branchIndex])
                                         : content.logicBranches[branchIndex]);
    }
    if (!content.checkMapTrackerId.empty()) {
        accumulateWidth(fmt::format("Tracker ID: {}", content.checkMapTrackerId));
    }
    if (!content.packCheckName.empty()) {
        accumulateWidth(fmt::format("Pack: {}", content.packCheckName));
    }

    return std::max(contentWidth, 1.0f);
}

static TooltipPlacement ComputeTooltipPlacement(const ImVec2& anchorMin, const ImVec2& anchorMax,
                                                const ImGuiViewport* viewport, float desiredWindowWidth,
                                                bool hasAvoidRect, const ImVec2& avoidRectMin,
                                                const ImVec2& avoidRectMax) {
    constexpr float popupOffsetX = 10.0f;
    constexpr float popupOffsetY = 4.0f;
    constexpr float viewportMargin = 8.0f;

    if (viewport == nullptr) {
        return { TooltipPlacementMode::Right, desiredWindowWidth };
    }

    const float usableMinX = viewport->WorkPos.x + viewportMargin;
    const float usableMaxX = viewport->WorkPos.x + viewport->WorkSize.x - viewportMargin;
    const float leftBoundaryX = hasAvoidRect ? avoidRectMin.x : anchorMin.x;
    const float rightBoundaryX = hasAvoidRect ? avoidRectMax.x : anchorMax.x;
    const float availableRightWidth = std::max(0.0f, usableMaxX - (rightBoundaryX + popupOffsetX));
    const float availableLeftWidth = std::max(0.0f, (leftBoundaryX - popupOffsetX) - usableMinX);

    TooltipPlacement placement;
    if (desiredWindowWidth <= availableRightWidth) {
        placement.mode = TooltipPlacementMode::Right;
        placement.windowWidth = desiredWindowWidth;
        return placement;
    }
    if (desiredWindowWidth <= availableLeftWidth) {
        placement.mode = TooltipPlacementMode::Left;
        placement.windowWidth = desiredWindowWidth;
        return placement;
    }

    const float viewportWindowWidth = std::max(64.0f, usableMaxX - usableMinX);
    if (!hasAvoidRect) {
        const float usableMinY = viewport->WorkPos.y + viewportMargin;
        const float usableMaxY = viewport->WorkPos.y + viewport->WorkSize.y - viewportMargin;
        const float availableBelowHeight = std::max(0.0f, usableMaxY - (anchorMax.y + popupOffsetY));
        const float availableAboveHeight = std::max(0.0f, (anchorMin.y - popupOffsetY) - usableMinY);
        placement.mode =
            availableBelowHeight >= availableAboveHeight ? TooltipPlacementMode::Below : TooltipPlacementMode::Above;
        placement.windowWidth = std::min(desiredWindowWidth, viewportWindowWidth);
        return placement;
    }

    placement.mode = availableRightWidth >= availableLeftWidth ? TooltipPlacementMode::Right : TooltipPlacementMode::Left;
    placement.windowWidth = placement.mode == TooltipPlacementMode::Right ? availableRightWidth : availableLeftWidth;
    placement.windowWidth =
        placement.windowWidth > 0.0f ? std::min(placement.windowWidth, viewportWindowWidth) : viewportWindowWidth;
    return placement;
}

static MarkerTooltipLayout ComputeMarkerTooltipLayout(const MarkerTooltipContent& content, float windowWidth,
                                                      const ImGuiViewport* viewport) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineHeight = GetCheckTrackerTextLineHeight();
    const float separatorHeight = (style.ItemSpacing.y * 2.0f) + 2.0f;
    float maxWindowHeight = 760.0f;
    if (viewport != nullptr) {
        maxWindowHeight =
            std::max(120.0f, viewport->WorkSize.y * CHECK_TRACKER_MAP_TOOLTIP_MAX_VIEWPORT_HEIGHT_RATIO);
    }

    const float contentWidth = std::max(1.0f, windowWidth - (style.WindowPadding.x * 2.0f) - 4.0f);

    float contentHeight = 0.0f;
    bool hasSection = false;
    auto addSectionHeight = [&](float sectionHeight) {
        if (hasSection) {
            contentHeight += separatorHeight;
        }
        contentHeight += sectionHeight;
        hasSection = true;
    };

    if (content.showTitle && !content.checkName.empty()) {
        const float titleHeight = CalcCheckTrackerTextSize(content.checkName, contentWidth).y;
        addSectionHeight(std::max(lineHeight, titleHeight));
    }
    if (!content.requirementSummary.empty()) {
        const float requirementHeight = CalcCheckTrackerTextSize(content.requirementSummary, contentWidth).y;
        addSectionHeight(std::max(lineHeight, requirementHeight));
    }
    if (!content.extraText.empty()) {
        std::string extraLabel = fmt::format("({})", content.extraText);
        float extraHeight = CalcCheckTrackerTextSize(extraLabel, contentWidth).y;
        addSectionHeight(std::max(lineHeight, extraHeight));
    }
    if (!content.hintText.empty() || content.showHintPrompt) {
        const std::string hintLabel =
            content.hintText.empty() ? "Right click to show hint." : fmt::format("Hint: {}", content.hintText);
        float hintHeight = CalcCheckTrackerTextSize(hintLabel, contentWidth).y;
        addSectionHeight(std::max(lineHeight, hintHeight));
    }
    if (!content.logicBranches.empty()) {
        float logicSectionHeight = 0.0f;
        for (size_t branchIndex = 0; branchIndex < content.logicBranches.size(); branchIndex++) {
            std::string branchText =
                branchIndex == 0 ? fmt::format("Logic: {}", content.logicBranches[branchIndex])
                                 : content.logicBranches[branchIndex];
            float logicHeight = CalcCheckTrackerTextSize(branchText, contentWidth).y;
            logicSectionHeight += std::max(lineHeight, logicHeight);
            if (branchIndex + 1 < content.logicBranches.size()) {
                logicSectionHeight += separatorHeight + lineHeight;
            }
        }
        addSectionHeight(logicSectionHeight);
    }
    if (!content.checkMapTrackerId.empty()) {
        float debugSectionHeight = lineHeight;
        if (!content.packCheckName.empty()) {
            debugSectionHeight += style.ItemSpacing.y + lineHeight;
        }
        addSectionHeight(debugSectionHeight);
    }

    MarkerTooltipLayout layout;
    layout.contentWidth = contentWidth;
    layout.estimatedWindowSize.x = windowWidth;
    layout.estimatedWindowSize.y = std::min(contentHeight + (style.WindowPadding.y * 2.0f) + 2.0f, maxWindowHeight);
    layout.maxWindowSize = ImVec2(windowWidth, maxWindowHeight);
    return layout;
}

static void DrawMarkerTooltip(const MarkerTooltipContent& content, const ImVec2& anchorMin, const ImVec2& anchorMax,
                              const ImGuiViewport* viewport, bool hasAvoidRect = false,
                              const ImVec2& avoidRectMin = ImVec2(0.0f, 0.0f),
                              const ImVec2& avoidRectMax = ImVec2(0.0f, 0.0f)) {
    if (!HasMarkerTooltipVisibleContent(content)) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float tooltipWidthSafetyPadding = hasAvoidRect ? 24.0f : 12.0f;
    float desiredWindowWidth =
        ComputeTooltipDesiredContentWidth(content) + (style.WindowPadding.x * 2.0f) + tooltipWidthSafetyPadding;
    if (viewport != nullptr) {
        constexpr float viewportMargin = 8.0f;
        desiredWindowWidth = std::min(desiredWindowWidth, viewport->WorkSize.x - (viewportMargin * 2.0f));
    }

    const TooltipPlacement placement = ComputeTooltipPlacement(anchorMin, anchorMax, viewport, desiredWindowWidth,
                                                               hasAvoidRect, avoidRectMin, avoidRectMax);
    MarkerTooltipLayout layout = ComputeMarkerTooltipLayout(content, placement.windowWidth, viewport);
    if (viewport != nullptr) {
        ImGui::SetNextWindowViewport(viewport->ID);
    }

    constexpr float popupOffsetX = 10.0f;
    constexpr float popupOffsetY = 4.0f;
    const float leftBoundaryX = hasAvoidRect ? avoidRectMin.x : anchorMin.x;
    const float rightBoundaryX = hasAvoidRect ? avoidRectMax.x : anchorMax.x;
    ImVec2 tooltipPosition = anchorMin;
    switch (placement.mode) {
        case TooltipPlacementMode::Right:
            tooltipPosition = ImVec2(rightBoundaryX + popupOffsetX, anchorMin.y - popupOffsetY);
            break;
        case TooltipPlacementMode::Left:
            tooltipPosition = ImVec2(leftBoundaryX - layout.estimatedWindowSize.x - popupOffsetX,
                                     anchorMin.y - popupOffsetY);
            break;
        case TooltipPlacementMode::Below:
            tooltipPosition = ImVec2(anchorMin.x, anchorMax.y + popupOffsetY);
            break;
        case TooltipPlacementMode::Above:
            tooltipPosition = ImVec2(anchorMin.x, anchorMin.y - layout.estimatedWindowSize.y - popupOffsetY);
            break;
    }
    if (viewport != nullptr) {
        constexpr float viewportMargin = 8.0f;
        const float minX = viewport->WorkPos.x + viewportMargin;
        const float maxX =
            std::max(minX, (viewport->WorkPos.x + viewport->WorkSize.x) - layout.estimatedWindowSize.x - viewportMargin);
        const float minY = viewport->WorkPos.y + viewportMargin;
        const float maxY =
            std::max(minY, (viewport->WorkPos.y + viewport->WorkSize.y) - layout.estimatedWindowSize.y - viewportMargin);
        tooltipPosition.x = std::clamp(tooltipPosition.x, minX, maxX);
        tooltipPosition.y = std::clamp(tooltipPosition.y, minY, maxY);
    }
    ImGui::SetNextWindowPos(tooltipPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(layout.estimatedWindowSize.x, 0.0f),
                                        ImVec2(layout.estimatedWindowSize.x, layout.maxWindowSize.y));
    ImGuiWindowFlags tooltipFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("MapMarkerTooltip##CheckTrackerMap", nullptr, tooltipFlags);
    ApplyCheckTrackerFontScaleToCurrentWindow();
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    bool hasRenderedSection = false;
    auto pushTooltipWrap = [&]() {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(1.0f, layout.contentWidth));
    };
    auto popTooltipWrap = [&]() {
        ImGui::PopTextWrapPos();
    };
    auto drawWrappedText = [&](const std::string& text) {
        pushTooltipWrap();
        ImGui::TextUnformatted(text.c_str());
        popTooltipWrap();
    };
    auto drawWrappedDisabledText = [&](const std::string& text) {
        pushTooltipWrap();
        ImGui::TextDisabled("%s", text.c_str());
        popTooltipWrap();
    };
    auto beginSection = [&]() {
        if (hasRenderedSection) {
            ImGui::Separator();
        }
        hasRenderedSection = true;
    };

    if (content.showTitle && !content.checkName.empty()) {
        beginSection();
        drawWrappedText(content.checkName);
    }

    if (!content.requirementSummary.empty()) {
        beginSection();
        drawWrappedDisabledText(content.requirementSummary);
    }

    if (!content.extraText.empty()) {
        beginSection();
        std::string extraLabel = fmt::format("({})", content.extraText);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(content.extraColor.r / 255.0f, content.extraColor.g / 255.0f,
                                     content.extraColor.b / 255.0f, content.extraColor.a / 255.0f));
        drawWrappedText(extraLabel);
        ImGui::PopStyleColor();
    }

    if (!content.hintText.empty() || content.showHintPrompt) {
        beginSection();
        if (!content.hintText.empty()) {
            std::string hintLabel = fmt::format("Hint: {}", content.hintText);
            drawWrappedText(hintLabel);
        } else {
            drawWrappedDisabledText("Right click to show hint.");
        }
    }

    if (!content.logicBranches.empty()) {
        beginSection();
        for (size_t branchIndex = 0; branchIndex < content.logicBranches.size(); branchIndex++) {
            if (branchIndex == 0) {
                std::string branchText = fmt::format("Logic: {}", content.logicBranches[branchIndex]);
                drawWrappedText(branchText);
                ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                                   "Logic:");
            } else {
                ImGui::TextDisabled("%s", "----- OR -----");
                drawWrappedText(content.logicBranches[branchIndex]);
            }
        }
    }

    if (!content.checkMapTrackerId.empty()) {
        beginSection();
        drawWrappedDisabledText(fmt::format("Tracker ID: {}", content.checkMapTrackerId));
        if (!content.packCheckName.empty()) {
            drawWrappedDisabledText(fmt::format("Pack: {}", content.packCheckName));
        }
    }

    ImGui::End();
}

static void QueueMarkerTooltip(const MarkerTooltipContent& content, const ImVec2& anchorMin, const ImVec2& anchorMax,
                               const ImGuiViewport* viewport, bool hasAvoidRect = false,
                               const ImVec2& avoidRectMin = ImVec2(0.0f, 0.0f),
                               const ImVec2& avoidRectMax = ImVec2(0.0f, 0.0f)) {
    pendingMarkerTooltip.active = true;
    pendingMarkerTooltip.content = content;
    pendingMarkerTooltip.anchorMin = anchorMin;
    pendingMarkerTooltip.anchorMax = anchorMax;
    pendingMarkerTooltip.viewport = viewport;
    pendingMarkerTooltip.hasAvoidRect = hasAvoidRect;
    pendingMarkerTooltip.avoidRectMin = avoidRectMin;
    pendingMarkerTooltip.avoidRectMax = avoidRectMax;
}

static void FlushQueuedMarkerTooltip() {
    if (!pendingMarkerTooltip.active) {
        return;
    }

    DrawMarkerTooltip(pendingMarkerTooltip.content, pendingMarkerTooltip.anchorMin, pendingMarkerTooltip.anchorMax,
                      pendingMarkerTooltip.viewport, pendingMarkerTooltip.hasAvoidRect, pendingMarkerTooltip.avoidRectMin,
                      pendingMarkerTooltip.avoidRectMax);
    pendingMarkerTooltip.active = false;
}

static void QueueRenderableMapMarkerTooltip(const RenderableMapMarker& renderableMarker, const ImVec2& anchorMin,
                                            const ImVec2& anchorMax, const ImGuiViewport* viewport) {
    QueueMarkerTooltip(BuildMarkerTooltipContent(renderableMarker.marker->check, renderableMarker.marker->packCheckName),
                       anchorMin, anchorMax, viewport);
}

static float ComputeMarkerHalfSize(const MapMarker& marker, float imageScale, bool isMultiMarkerCluster) {
    float markerPixelSize = std::max(CHECK_TRACKER_MAP_MIN_MARKER_PIXEL_SIZE, std::max(0.0f, marker.size) * imageScale);
    if (isMultiMarkerCluster) {
        markerPixelSize *= CHECK_TRACKER_MAP_MULTI_MARKER_SIZE_SCALE;
    }
    return markerPixelSize * 0.5f;
}

static std::vector<ImU32> BuildClusterSegmentColors(const std::vector<RenderableMapMarker>& renderableMarkers) {
    bool hasAvailable = false;
    bool hasRequirementMismatch = false;
    bool hasUnavailable = false;
    bool hasDone = false;

    for (const auto& renderableMarker : renderableMarkers) {
        if (renderableMarker.isDone) {
            hasDone = true;
        } else if (renderableMarker.isRequirementMismatch) {
            hasRequirementMismatch = true;
        } else if (renderableMarker.isAvailable) {
            hasAvailable = true;
        } else {
            hasUnavailable = true;
        }
    }

    std::vector<ImU32> segmentColors;
    if (hasAvailable) {
        segmentColors.push_back(CHECK_TRACKER_MAP_COLOR_AVAILABLE);
    }
    if (hasRequirementMismatch) {
        segmentColors.push_back(CHECK_TRACKER_MAP_COLOR_AGE_MISMATCH);
    }
    if (hasUnavailable) {
        segmentColors.push_back(CHECK_TRACKER_MAP_COLOR_UNAVAILABLE);
    }
    const bool hasNonDoneSegment = hasAvailable || hasRequirementMismatch || hasUnavailable;
    if (!hasNonDoneSegment && hasDone) {
        segmentColors.push_back(CHECK_TRACKER_MAP_COLOR_DONE);
    }
    if (segmentColors.empty()) {
        segmentColors.push_back(CHECK_TRACKER_MAP_COLOR_UNAVAILABLE);
    }
    return segmentColors;
}

static void DrawClusterPopupTargetHeader(const std::optional<int>& popupNavigationTargetTabIndex,
                                         const std::optional<std::string>& requirementSummary = std::nullopt) {
    if (!popupNavigationTargetTabIndex.has_value() || *popupNavigationTargetTabIndex < 0 ||
        *popupNavigationTargetTabIndex >= static_cast<int>(mapTrackerState.tabs.size())) {
        return;
    }

    const MapTabData& popupTargetTab = mapTrackerState.tabs[static_cast<size_t>(*popupNavigationTargetTabIndex)];
    const float contentWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    ImGui::TextUnformatted(popupTargetTab.mapName.c_str());
    ImGui::PopTextWrapPos();
    if (requirementSummary.has_value() && !requirementSummary->empty()) {
        const std::string summaryLabel = BuildPopupExtraLabel(*requirementSummary);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextDisabled("%s", summaryLabel.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
}

static std::vector<const RenderableMapMarker*> BuildPopupRenderableMarkersWithoutDuplicateChecks(
    const std::vector<RenderableMapMarker>& renderableMarkers) {
    std::vector<const RenderableMapMarker*> uniqueRenderableMarkers;
    uniqueRenderableMarkers.reserve(renderableMarkers.size());

    std::unordered_set<uint32_t> seenChecks;
    seenChecks.reserve(renderableMarkers.size());

    for (const auto& renderableMarker : renderableMarkers) {
        const uint32_t checkId = static_cast<uint32_t>(renderableMarker.marker->check);
        if (seenChecks.insert(checkId).second) {
            uniqueRenderableMarkers.push_back(&renderableMarker);
        }
    }

    return uniqueRenderableMarkers;
}

static bool IsMouseInsidePopupWindowRect(const ImVec2& popupPosition, const ImVec2& popupWindowSize,
                                         const ImVec2& mousePosition) {
    return mousePosition.x >= popupPosition.x && mousePosition.x <= (popupPosition.x + popupWindowSize.x) &&
           mousePosition.y >= popupPosition.y && mousePosition.y <= (popupPosition.y + popupWindowSize.y);
}

static bool IsMouseInsidePopupBridgeRect(const ImVec2& anchorMin, const ImVec2& anchorMax, const ImVec2& popupPosition,
                                         const ImVec2& popupWindowSize, const ImVec2& mousePosition) {
    constexpr float bridgePadding = 6.0f;

    const float minX = std::min(anchorMin.x, popupPosition.x) - bridgePadding;
    const float minY = std::min(anchorMin.y, popupPosition.y) - bridgePadding;
    const float maxX = std::max(anchorMax.x, popupPosition.x + popupWindowSize.x) + bridgePadding;
    const float maxY = std::max(anchorMax.y, popupPosition.y + popupWindowSize.y) + bridgePadding;

    return mousePosition.x >= minX && mousePosition.x <= maxX && mousePosition.y >= minY && mousePosition.y <= maxY;
}

static PopupWindowLayout ComputeClusterPopupLayout(const std::vector<const RenderableMapMarker*>& clusterMarkers,
                                                   const std::optional<int>& popupNavigationTargetTabIndex,
                                                   const std::optional<std::string>& requirementSummary,
                                                   const ImGuiViewport* viewport) {
    const ImGuiStyle& popupStyle = ImGui::GetStyle();
    float popupTextLineHeight = ImGui::GetTextLineHeight();
    float popupStatusWidth = std::max(10.0f, popupTextLineHeight - 2.0f);
    const float popupStatusSpacing = std::max(6.0f, popupStyle.ItemInnerSpacing.x);

    float desiredContentWidth = ComputePopupHeaderDesiredContentWidth(popupNavigationTargetTabIndex, requirementSummary);

    for (size_t clusterIndex = 0; clusterIndex < clusterMarkers.size(); clusterIndex++) {
        const RenderableMapMarker& renderableMarker = *clusterMarkers[clusterIndex];
        const MapMarker& marker = *renderableMarker.marker;

        std::string checkName = GetCheckDisplayName(marker.check);
        float rowWidth = popupStatusWidth + popupStatusSpacing + CalcPopupTextSize(checkName).x;

        std::string extraText = GetCheckExtraInfoText(marker.check);
        if (!extraText.empty()) {
            std::string extraLabel = BuildPopupExtraLabel(extraText);
            rowWidth += popupStyle.ItemSpacing.x + CalcPopupTextSize(extraLabel).x;
        }

        desiredContentWidth = std::max(desiredContentWidth, rowWidth);
    }

    const bool hasHeader = popupNavigationTargetTabIndex.has_value();
    const float minPopupWidth = hasHeader ? 180.0f : 96.0f;
    const float minPopupHeight = hasHeader ? 90.0f : 24.0f;
    float maxPopupWidth = 900.0f;
    float maxPopupHeight = 760.0f;
    if (viewport != nullptr) {
        maxPopupWidth = std::max(minPopupWidth, viewport->WorkSize.x * 0.75f);
        maxPopupHeight = std::max(minPopupHeight, viewport->WorkSize.y * 0.75f);
    }

    PopupWindowLayout layout;
    layout.windowSize.x = std::clamp(desiredContentWidth + (popupStyle.WindowPadding.x * 2.0f) + 4.0f, minPopupWidth,
                                     maxPopupWidth);
    auto computeMeasuredRowContentHeight = [&](float rowContentWidth) {
        float rowContentHeight = 0.0f;
        for (size_t clusterIndex = 0; clusterIndex < clusterMarkers.size(); clusterIndex++) {
            const RenderableMapMarker& renderableMarker = *clusterMarkers[clusterIndex];
            const MapMarker& marker = *renderableMarker.marker;
            const PopupRowLayout rowLayout = ComputePopupRowLayout(GetCheckDisplayName(marker.check),
                                                                   GetCheckExtraInfoText(marker.check), rowContentWidth);
            rowContentHeight += rowLayout.rowHeight;
            if (clusterIndex + 1 < clusterMarkers.size()) {
                rowContentHeight += popupStyle.ItemSpacing.y;
            }
        }
        return rowContentHeight;
    };

    float popupContentWidth = GetPopupContentWidthFromWindowWidth(layout.windowSize.x);
    float measuredHeaderHeight =
        ComputePopupHeaderHeight(popupNavigationTargetTabIndex, requirementSummary, popupContentWidth);
    float measuredRowContentHeight = computeMeasuredRowContentHeight(popupContentWidth);
    float availableChildHeight =
        std::max(popupTextLineHeight, maxPopupHeight - (popupStyle.WindowPadding.y * 2.0f) - measuredHeaderHeight);

    if (measuredRowContentHeight > availableChildHeight) {
        layout.windowSize.x = std::min(maxPopupWidth, layout.windowSize.x + popupStyle.ScrollbarSize);
        popupContentWidth = GetPopupContentWidthFromWindowWidth(layout.windowSize.x);
        measuredHeaderHeight = ComputePopupHeaderHeight(popupNavigationTargetTabIndex, requirementSummary, popupContentWidth);

        // When the list scrolls, the child window loses horizontal space to the scrollbar.
        // Reserve that width up front so borderline rows do not wrap after the scrollbar appears.
        const float scrollableRowContentWidth = std::max(24.0f, popupContentWidth - popupStyle.ScrollbarSize);
        measuredRowContentHeight = computeMeasuredRowContentHeight(scrollableRowContentWidth);
        availableChildHeight =
            std::max(popupTextLineHeight, maxPopupHeight - (popupStyle.WindowPadding.y * 2.0f) - measuredHeaderHeight);
    }

    layout.childHeight = std::clamp(measuredRowContentHeight, popupTextLineHeight, availableChildHeight);
    layout.windowSize.y = std::clamp(measuredHeaderHeight + layout.childHeight + (popupStyle.WindowPadding.y * 2.0f) + 2.0f,
                                     minPopupHeight, maxPopupHeight);
    return layout;
}

static std::optional<size_t> FindMapLinkIndexByTargetTabId(const MapTabData& sourceTab, const std::string& targetTabId) {
    for (size_t linkIndex = 0; linkIndex < sourceTab.links.size(); linkIndex++) {
        if (sourceTab.links[linkIndex].targetMapId == targetTabId) {
            return linkIndex;
        }
    }

    return std::nullopt;
}

static void NavigateToMapTab(int targetTabIndex) {
    if (targetTabIndex < 0 || targetTabIndex >= static_cast<int>(mapTrackerState.tabs.size())) {
        return;
    }

    mapTrackerState.selectedTabIndex = targetTabIndex;
    const MapTabData& targetTab = mapTrackerState.tabs[static_cast<size_t>(targetTabIndex)];
    mapTrackerState.selectedGroupName = targetTab.groupName;
    if (!mapTrackerState.selectedGroupName.empty()) {
        mapTrackerState.lastSelectedTabByGroup[mapTrackerState.selectedGroupName] = targetTabIndex;
    }
    mapTrackerState.requestedTabId = targetTab.mapId;
}

static void DrawRenderableMarkerRows(const std::vector<const RenderableMapMarker*>& renderableMarkers) {
    const ImGuiViewport* currentViewport = ImGui::GetWindowViewport();

    for (size_t markerIndex = 0; markerIndex < renderableMarkers.size(); markerIndex++) {
        const RenderableMapMarker& renderableMarker = *renderableMarkers[markerIndex];
        const MapMarker& marker = *renderableMarker.marker;
        bool canToggle = CanToggleSkippedStateForCheck(marker.check);
        std::string checkName = GetCheckDisplayName(marker.check);
        std::string extraText = GetCheckExtraInfoText(marker.check);
        std::string markerRowId = fmt::format("PopupCheckRow_{}_{}_{}_{}", static_cast<int>(marker.check), markerIndex,
                                              marker.packCheckName, marker.mapId);

        ImGui::PushID(markerRowId.c_str());
        const float rowAvailableWidth = std::max(48.0f, ImGui::GetContentRegionAvail().x);
        const PopupRowLayout rowLayout = ComputePopupRowLayout(checkName, extraText, rowAvailableWidth);
        const std::string extraLabel = BuildPopupExtraLabel(extraText);

        ImGui::BeginGroup();
        if (!canToggle) {
            ImGui::BeginDisabled();
        }

        ImVec2 statusSize(rowLayout.statusSize, rowLayout.statusSize);
        bool statusPressed = ImGui::ColorButton("##Status", ImGui::ColorConvertU32ToFloat4(renderableMarker.fillColor),
                                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                                statusSize);
        bool statusHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImVec2 statusRectMin = ImGui::GetItemRectMin();
        ImVec2 statusRectMax = ImGui::GetItemRectMax();
        ImGui::SameLine(0.0f, rowLayout.statusSpacing);

        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowLayout.checkNameWrapWidth);
        ImGui::TextUnformatted(checkName.c_str());
        ImGui::PopTextWrapPos();

        bool extraHovered = false;
        ImVec2 extraRectMin = ImGui::GetItemRectMin();
        ImVec2 extraRectMax = ImGui::GetItemRectMax();
        if (!extraLabel.empty()) {
            Color_RGBA8 legacyExtraColor = GetLegacyCheckExtraColor(marker.check);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(legacyExtraColor.r / 255.0f, legacyExtraColor.g / 255.0f, legacyExtraColor.b / 255.0f,
                       legacyExtraColor.a / 255.0f));
            if (rowLayout.extraOnSameLine) {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
                ImGui::TextUnformatted(extraLabel.c_str());
            } else {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowLayout.textAvailableWidth);
                ImGui::TextUnformatted(extraLabel.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::PopStyleColor();
            extraHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            extraRectMin = ImGui::GetItemRectMin();
            extraRectMax = ImGui::GetItemRectMax();
        }

        ImGui::EndGroup();

        if (!canToggle) {
            ImGui::EndDisabled();
        }
        ImGui::EndGroup();

        bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImVec2 rowRectMin = ImGui::GetItemRectMin();
        ImVec2 rowRectMax = ImGui::GetItemRectMax();
        bool rowPressed = rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                          !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f);
        if ((statusPressed || (!statusPressed && rowPressed)) && canToggle) {
            ToggleSkippedStateForCheck(marker.check);
        }

        bool rowTooltipHovered = statusHovered || rowHovered || extraHovered;
        bool hintTogglePressed = rowTooltipHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                                 !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
        if (rowTooltipHovered) {
            if (hintTogglePressed) {
                ToggleMapTrackerCheckHint(marker.check);
            }
            ImVec2 tooltipAnchorMin = statusRectMin;
            ImVec2 tooltipAnchorMax = statusRectMax;
            if (extraHovered) {
                tooltipAnchorMin = extraRectMin;
                tooltipAnchorMax = extraRectMax;
            } else if (rowHovered) {
                tooltipAnchorMin = rowRectMin;
                tooltipAnchorMax = rowRectMax;
            }
            const ImVec2 popupWindowMin = ImGui::GetWindowPos();
            const ImVec2 popupWindowMax(popupWindowMin.x + ImGui::GetWindowSize().x,
                                        popupWindowMin.y + ImGui::GetWindowSize().y);
            MarkerTooltipContent tooltipContent = BuildPopupRowTooltipContent(marker.check);
            if (HasMarkerTooltipVisibleContent(tooltipContent)) {
                QueueMarkerTooltip(tooltipContent, tooltipAnchorMin, tooltipAnchorMax, currentViewport, true,
                                   popupWindowMin, popupWindowMax);
            }
        }

        ImGui::PopID();
    }
}

void DrawMapTabContent(int tabIndex, const MapTrackerRenderCache& renderCache) {
    pendingMarkerTooltip.active = false;

    MapTabData& tab = mapTrackerState.tabs[static_cast<size_t>(tabIndex)];
    const CachedMapTabRenderData& cachedTabRenderData = renderCache.tabRenderDataByIndex[static_cast<size_t>(tabIndex)];

    if (!tab.imageLoaded) {
        ImGui::TextWrapped("Could not render map image for \"%s\".", tab.mapName.c_str());
        if (!tab.imageError.empty()) {
            ImGui::TextWrapped("%s", tab.imageError.c_str());
        }
        return;
    }

    tab.zoomFactor = std::clamp(tab.zoomFactor, CHECK_TRACKER_MAP_ZOOM_MIN, CHECK_TRACKER_MAP_ZOOM_MAX);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    availableSize.x = std::max(1.0f, availableSize.x);
    availableSize.y = std::max(120.0f, availableSize.y);

    ImGuiWindowFlags mapCanvasFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    std::string mapCanvasId = "CheckTrackerMapCanvas##" + tab.mapId;
    ImGui::BeginChild(mapCanvasId.c_str(), availableSize, false, mapCanvasFlags);
    ApplyCheckTrackerFontScaleToCurrentWindow();
    const ImGuiViewport* currentViewport = ImGui::GetWindowViewport();

    availableSize = ImGui::GetContentRegionAvail();
    // In a scrollable table cell, available Y can grow with scroll offset. Clamp to a stable visible height so
    // map scaling does not increase while scrolling.
    float visibleRegionHeight = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
    if (visibleRegionHeight > 0.0f) {
        availableSize.y = std::clamp(availableSize.y, 1.0f, visibleRegionHeight);
    }
    const float fitPaddingX = 10.0f;
    const float fitPaddingY = ImGui::GetStyle().ItemSpacing.y + 8.0f;
    ImVec2 fitSize = availableSize;
    fitSize.x = std::max(1.0f, fitSize.x - (fitPaddingX * 2.0f));
    fitSize.y = std::max(1.0f, fitSize.y - fitPaddingY);

    float widthScale = (fitSize.x > 0.0f && tab.textureSize.x > 0.0f) ? (fitSize.x / tab.textureSize.x) : 1.0f;
    float heightScale = (fitSize.y > 0.0f && tab.textureSize.y > 0.0f) ? (fitSize.y / tab.textureSize.y) : widthScale;
    float fitScale = std::min(widthScale, heightScale);
    fitScale = std::clamp(fitScale, 0.05f, 1.0f);

    ImVec2 mapCursorPos = ImGui::GetCursorPos();
    ImVec2 mapCursorScreenPos = ImGui::GetCursorScreenPos();

    auto clampPanOffset = [&](const ImVec2& currentDrawSize, float currentHorizontalPadding, float currentVerticalPadding) {
        float minPanX = 0.0f;
        float maxPanX = 0.0f;
        if (currentDrawSize.x > availableSize.x) {
            minPanX = availableSize.x - currentDrawSize.x - currentHorizontalPadding;
            maxPanX = -currentHorizontalPadding;
        }

        float minPanY = 0.0f;
        float maxPanY = 0.0f;
        if (currentDrawSize.y > availableSize.y) {
            minPanY = availableSize.y - currentDrawSize.y - currentVerticalPadding;
            maxPanY = -currentVerticalPadding;
        }

        tab.panOffset.x = std::clamp(tab.panOffset.x, minPanX, maxPanX);
        tab.panOffset.y = std::clamp(tab.panOffset.y, minPanY, maxPanY);
    };

    float imageScale = fitScale * tab.zoomFactor;
    ImVec2 drawSize(tab.textureSize.x * imageScale, tab.textureSize.y * imageScale);
    float horizontalPadding = std::max(0.0f, (availableSize.x - drawSize.x) * 0.5f);
    float verticalPadding = std::max(0.0f, (availableSize.y - drawSize.y) * 0.5f);
    ImVec2 imageStartPos(mapCursorScreenPos.x + horizontalPadding + tab.panOffset.x,
                         mapCursorScreenPos.y + verticalPadding + tab.panOffset.y);

    if (mapClusterPopupState.tabId != tab.mapId) {
        CloseClusterPopup();
        mapClusterPopupState.tabId = tab.mapId;
    }
    if (mapLinkPopupState.tabId != tab.mapId) {
        CloseLinkPopup();
        mapLinkPopupState.tabId = tab.mapId;
    }

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseOverPopupWindow = false;

    if (mapClusterPopupState.open && mapClusterPopupState.tabId == tab.mapId) {
        auto popupClusterIt = cachedTabRenderData.renderableMarkersByStackKey.find(mapClusterPopupState.stackKey);
        if (popupClusterIt != cachedTabRenderData.renderableMarkersByStackKey.end() && !popupClusterIt->second.empty()) {
            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(popupClusterIt->second);
            if (!popupMarkers.empty()) {
                if (mapClusterPopupState.hasLastWindowRect) {
                    mouseOverPopupWindow |= IsMouseInsidePopupWindowRect(mapClusterPopupState.lastWindowPosition,
                                                                        mapClusterPopupState.lastWindowSize, mousePos);
                } else {
                    const PopupWindowLayout popupLayout =
                        ComputeClusterPopupLayout(popupMarkers, std::nullopt, std::nullopt, currentViewport);
                    mouseOverPopupWindow |= IsMouseInsidePopupWindowRect(mapClusterPopupState.popupPosition,
                                                                        popupLayout.windowSize, mousePos);
                }
            }
        }
    }

    if (mapLinkPopupState.open && mapLinkPopupState.tabId == tab.mapId) {
        auto targetTabIndexIt = mapTrackerState.tabIndexById.find(mapLinkPopupState.targetTabId);
        if (targetTabIndexIt != mapTrackerState.tabIndexById.end()) {
            const int targetTabIndex = static_cast<int>(targetTabIndexIt->second);
            const CachedMapTabRenderData& targetTabRenderData =
                renderCache.tabRenderDataByIndex[static_cast<size_t>(targetTabIndex)];
            std::optional<std::string> requirementSummary = std::nullopt;
            if (auto popupLinkIndex = FindMapLinkIndexByTargetTabId(tab, mapLinkPopupState.targetTabId);
                popupLinkIndex.has_value() && *popupLinkIndex < cachedTabRenderData.linkRequirementSummariesByIndex.size()) {
                requirementSummary = cachedTabRenderData.linkRequirementSummariesByIndex[*popupLinkIndex];
            }

            const auto popupMarkers =
                BuildPopupRenderableMarkersWithoutDuplicateChecks(targetTabRenderData.renderableMarkers);
            if (!popupMarkers.empty()) {
                if (mapLinkPopupState.hasLastWindowRect) {
                    mouseOverPopupWindow |= IsMouseInsidePopupWindowRect(mapLinkPopupState.lastWindowPosition,
                                                                        mapLinkPopupState.lastWindowSize, mousePos);
                } else {
                    const PopupWindowLayout popupLayout =
                        ComputeClusterPopupLayout(popupMarkers, targetTabIndex, requirementSummary, currentViewport);
                    mouseOverPopupWindow |= IsMouseInsidePopupWindowRect(mapLinkPopupState.popupPosition,
                                                                        popupLayout.windowSize, mousePos);
                }
            }
        }
    }

    float wheelDelta = ImGui::GetIO().MouseWheel;
    bool mouseInsideImage = mousePos.x >= imageStartPos.x && mousePos.x <= (imageStartPos.x + drawSize.x) &&
                            mousePos.y >= imageStartPos.y && mousePos.y <= (imageStartPos.y + drawSize.y);
    if (wheelDelta != 0.0f && mouseInsideImage && !mouseOverPopupWindow) {
        float previousZoomFactor = tab.zoomFactor;
        float zoomStep = std::pow(CHECK_TRACKER_MAP_ZOOM_WHEEL_STEP, wheelDelta);
        float nextZoomFactor =
            std::clamp(previousZoomFactor * zoomStep, CHECK_TRACKER_MAP_ZOOM_MIN, CHECK_TRACKER_MAP_ZOOM_MAX);
        if (nextZoomFactor != previousZoomFactor) {
            float previousImageScale = imageScale;
            ImVec2 previousImageStartPos = imageStartPos;

            float mapPixelX = (mousePos.x - previousImageStartPos.x) / std::max(0.0001f, previousImageScale);
            float mapPixelY = (mousePos.y - previousImageStartPos.y) / std::max(0.0001f, previousImageScale);

            tab.zoomFactor = nextZoomFactor;
            imageScale = fitScale * tab.zoomFactor;
            drawSize = ImVec2(tab.textureSize.x * imageScale, tab.textureSize.y * imageScale);
            horizontalPadding = std::max(0.0f, (availableSize.x - drawSize.x) * 0.5f);
            verticalPadding = std::max(0.0f, (availableSize.y - drawSize.y) * 0.5f);

            ImVec2 desiredImageStartPos(mousePos.x - (mapPixelX * imageScale), mousePos.y - (mapPixelY * imageScale));
            tab.panOffset.x = desiredImageStartPos.x - mapCursorScreenPos.x - horizontalPadding;
            tab.panOffset.y = desiredImageStartPos.y - mapCursorScreenPos.y - verticalPadding;
        }
    }

    clampPanOffset(drawSize, horizontalPadding, verticalPadding);

    ImGui::SetCursorPos(
        ImVec2(mapCursorPos.x + horizontalPadding + tab.panOffset.x, mapCursorPos.y + verticalPadding + tab.panOffset.y));
    imageStartPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::Image(tab.texture, drawSize);

    // Capture drag input on the map itself so the parent ImGui window does not move.
    ImGui::SetCursorScreenPos(imageStartPos);
    ImGui::PushID("MapPanLayer");
    ImGui::InvisibleButton("MapPanCapture", drawSize);
    ImGui::SetItemAllowOverlap();
    bool mapImageActive = ImGui::IsItemActive();
    ImGui::PopID();

    if (mapImageActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 dragDelta = ImGui::GetIO().MouseDelta;
        tab.panOffset.x += dragDelta.x;
        tab.panOffset.y += dragDelta.y;
        clampPanOffset(drawSize, horizontalPadding, verticalPadding);
    }

    std::optional<int> playerFocusTargetTabIndex;
    if (auto focusTargetTabId = ResolvePreferredMapTabIdForArea(currentArea); focusTargetTabId.has_value()) {
        auto focusTargetTabIndexIt = mapTrackerState.tabIndexById.find(*focusTargetTabId);
        if (focusTargetTabIndexIt != mapTrackerState.tabIndexById.end()) {
            playerFocusTargetTabIndex = static_cast<int>(focusTargetTabIndexIt->second);
        }
    }

    bool markerHoveredForPopup = false;
    bool linkHoveredForPopup = false;

    for (const auto& stackKey : cachedTabRenderData.stackOrder) {
        auto renderableMarkersIt = cachedTabRenderData.renderableMarkersByStackKey.find(stackKey);
        if (renderableMarkersIt == cachedTabRenderData.renderableMarkersByStackKey.end()) {
            continue;
        }

        const auto& renderableMarkers = renderableMarkersIt->second;
        if (renderableMarkers.empty()) {
            continue;
        }

        const MapMarker& anchorMarker = *renderableMarkers.front().marker;
        bool isMultiMarkerCluster = renderableMarkers.size() > 1;

        float halfSize = ComputeMarkerHalfSize(anchorMarker, imageScale, isMultiMarkerCluster);
        ImVec2 center(imageStartPos.x + (anchorMarker.x * imageScale), imageStartPos.y + (anchorMarker.y * imageScale));
        ImVec2 markerMin(center.x - halfSize, center.y - halfSize);
        ImVec2 markerMax(center.x + halfSize, center.y + halfSize);

        ImGui::SetCursorScreenPos(markerMin);
        ImGui::PushID(fmt::format("MapMarker_{}_{}", tab.mapId, stackKey).c_str());
        ImGui::InvisibleButton("marker", ImVec2(markerMax.x - markerMin.x, markerMax.y - markerMin.y));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f);
        bool rightClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                            !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
        ImGui::PopID();

        std::vector<ImU32> segmentColors = BuildClusterSegmentColors(renderableMarkers);

        if (segmentColors.size() == 1) {
            drawList->AddRectFilled(markerMin, markerMax, segmentColors.front(), 1.0f);
        } else {
            float markerWidth = markerMax.x - markerMin.x;
            for (size_t segmentIndex = 0; segmentIndex < segmentColors.size(); segmentIndex++) {
                float leftX = markerMin.x + (markerWidth * static_cast<float>(segmentIndex) /
                                             static_cast<float>(segmentColors.size()));
                float rightX = markerMin.x + (markerWidth * static_cast<float>(segmentIndex + 1) /
                                              static_cast<float>(segmentColors.size()));
                drawList->AddRectFilled(ImVec2(leftX, markerMin.y), ImVec2(rightX, markerMax.y),
                                        segmentColors[segmentIndex]);
            }
        }
        drawList->AddRect(markerMin, markerMax, CHECK_TRACKER_MAP_COLOR_BORDER, 1.0f, 0, 1.5f);

        if (isMultiMarkerCluster) {
            if (hovered) {
                markerHoveredForPopup = true;
                CloseLinkPopup();
                mapClusterPopupState.open = true;
                mapClusterPopupState.tabId = tab.mapId;
                mapClusterPopupState.stackKey = stackKey;
                mapClusterPopupState.anchorMin = markerMin;
                mapClusterPopupState.anchorMax = markerMax;
                const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(renderableMarkers);
                const PopupWindowLayout popupLayout =
                    ComputeClusterPopupLayout(popupMarkers, std::nullopt, std::nullopt, currentViewport);
                mapClusterPopupState.popupPosition =
                    ComputeAnchoredPopupPosition(markerMin, markerMax, popupLayout.windowSize, currentViewport);
                mapClusterPopupState.hasLastWindowRect = false;
            }
            continue;
        }

        if (clicked) {
            ToggleSkippedStateForCheck(anchorMarker.check);
        }

        if (hovered) {
            if (rightClicked) {
                ToggleMapTrackerCheckHint(anchorMarker.check);
            }
            QueueRenderableMapMarkerTooltip(renderableMarkers.front(), markerMin, markerMax, currentViewport);
        }
    }

    for (size_t linkIndex = 0; linkIndex < tab.links.size(); linkIndex++) {
        const MapLink& link = tab.links[linkIndex];
        auto targetTabIndexIt = mapTrackerState.tabIndexById.find(link.targetMapId);
        if (targetTabIndexIt == mapTrackerState.tabIndexById.end()) {
            continue;
        }

        int targetTabIndex = static_cast<int>(targetTabIndexIt->second);
        const MapTabData& targetTab = mapTrackerState.tabs[static_cast<size_t>(targetTabIndex)];
        const CachedMapTabRenderData& targetTabRenderData =
            renderCache.tabRenderDataByIndex[static_cast<size_t>(targetTabIndex)];
        const auto& linkRenderableMarkers = targetTabRenderData.renderableMarkers;
        std::vector<ImU32> segmentColors = BuildClusterSegmentColors(linkRenderableMarkers);
        bool isPlayerFocusLinkTarget =
            playerFocusTargetTabIndex.has_value() && (*playerFocusTargetTabIndex == targetTabIndex);

        float halfSize = std::max(CHECK_TRACKER_MAP_MIN_MARKER_PIXEL_SIZE, std::max(0.0f, link.size) * imageScale) * 0.5f;
        ImVec2 center(imageStartPos.x + (link.x * imageScale), imageStartPos.y + (link.y * imageScale));
        ImVec2 markerMin(center.x - halfSize, center.y - halfSize);
        ImVec2 markerMax(center.x + halfSize, center.y + halfSize);

        ImGui::SetCursorScreenPos(markerMin);
        ImGui::PushID(fmt::format("MapLink_{}_{}_{}", tab.mapId, link.targetMapId, linkIndex).c_str());
        ImGui::InvisibleButton("link", ImVec2(markerMax.x - markerMin.x, markerMax.y - markerMin.y));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f);
        ImGui::PopID();

        if (segmentColors.size() == 1) {
            drawList->AddCircleFilled(center, halfSize, segmentColors.front(), 16);
        } else {
            float startAngle = -IM_PI * 0.5f;
            float fullCircle = IM_PI * 2.0f;
            for (size_t segmentIndex = 0; segmentIndex < segmentColors.size(); segmentIndex++) {
                float segmentStart =
                    startAngle + (fullCircle * static_cast<float>(segmentIndex) / static_cast<float>(segmentColors.size()));
                float segmentEnd = startAngle + (fullCircle * static_cast<float>(segmentIndex + 1) /
                                                 static_cast<float>(segmentColors.size()));

                drawList->PathClear();
                drawList->PathLineTo(center);
                drawList->PathArcTo(center, halfSize, segmentStart, segmentEnd, 12);
                drawList->PathLineTo(center);
                drawList->PathFillConvex(segmentColors[segmentIndex]);
            }
        }
        MapLinkBorderStyle linkBorderStyle = GetMapLinkBorderStyle(cachedTabRenderData.linkAvailabilityByIndex[linkIndex]);
        drawList->AddCircle(center, halfSize, linkBorderStyle.color, 16, linkBorderStyle.thickness);
        if (isPlayerFocusLinkTarget) {
            float highlightRadius = halfSize + std::max(2.0f, halfSize * 0.22f);
            drawList->AddCircle(center, highlightRadius, IM_COL32(255, 255, 255, 255), 20, 3.0f);

            float arrowHalfWidth = std::max(3.0f, halfSize * 0.32f);
            float arrowHeight = std::max(4.0f, halfSize * 0.50f);
            ImVec2 arrowTip(center.x, center.y - highlightRadius - 1.0f);
            ImVec2 arrowLeft(center.x - arrowHalfWidth, arrowTip.y - arrowHeight);
            ImVec2 arrowRight(center.x + arrowHalfWidth, arrowTip.y - arrowHeight);
            drawList->AddTriangleFilled(arrowTip, arrowLeft, arrowRight, IM_COL32(255, 255, 255, 245));
        }

        if (clicked) {
            CloseClusterPopup();
            CloseLinkPopup();
            NavigateToMapTab(targetTabIndex);
            continue;
        }

        if (hovered) {
            linkHoveredForPopup = true;
            CloseClusterPopup();
            mapLinkPopupState.open = true;
            mapLinkPopupState.tabId = tab.mapId;
            mapLinkPopupState.targetTabId = link.targetMapId;
            mapLinkPopupState.anchorMin = markerMin;
            mapLinkPopupState.anchorMax = markerMax;
            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(linkRenderableMarkers);
            const PopupWindowLayout popupLayout =
                ComputeClusterPopupLayout(popupMarkers, targetTabIndex, cachedTabRenderData.linkRequirementSummariesByIndex[linkIndex],
                                          currentViewport);
            mapLinkPopupState.popupPosition =
                ComputeAnchoredPopupPosition(markerMin, markerMax, popupLayout.windowSize, currentViewport);
            mapLinkPopupState.hasLastWindowRect = false;
        }
    }

    bool popupHovered = false;
    bool popupBridgeHovered = false;
    if (mapClusterPopupState.open && mapClusterPopupState.tabId == tab.mapId) {
        auto popupClusterIt = cachedTabRenderData.renderableMarkersByStackKey.find(mapClusterPopupState.stackKey);
        if (popupClusterIt == cachedTabRenderData.renderableMarkersByStackKey.end() || popupClusterIt->second.empty()) {
            CloseClusterPopup();
        } else {
            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(popupClusterIt->second);
            const PopupWindowLayout popupLayout =
                ComputeClusterPopupLayout(popupMarkers, std::nullopt, std::nullopt, currentViewport);
            mapClusterPopupState.popupPosition =
                ClampWindowPositionToViewport(mapClusterPopupState.popupPosition, popupLayout.windowSize, currentViewport);

            if (currentViewport != nullptr) {
                ImGui::SetNextWindowViewport(currentViewport->ID);
            }
            ImGui::SetNextWindowPos(mapClusterPopupState.popupPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(popupLayout.windowSize, ImGuiCond_Always);
            ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            std::string popupTitle = "Map Checks##MapClusterPopup_" + tab.mapId;
            ImGui::Begin(popupTitle.c_str(), nullptr, popupFlags);
            ApplyCheckTrackerFontScaleToCurrentWindow();
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            mapClusterPopupState.lastWindowPosition = ImGui::GetWindowPos();
            mapClusterPopupState.lastWindowSize = ImGui::GetWindowSize();
            mapClusterPopupState.hasLastWindowRect = true;
            popupBridgeHovered = IsMouseInsidePopupBridgeRect(mapClusterPopupState.anchorMin, mapClusterPopupState.anchorMax,
                                                             mapClusterPopupState.lastWindowPosition,
                                                             mapClusterPopupState.lastWindowSize, mousePos);

            popupHovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);

            ImGui::BeginChild("MapClusterPopupRows", ImVec2(0.0f, popupLayout.childHeight), false,
                              ImGuiWindowFlags_NavFlattened);
            ApplyCheckTrackerFontScaleToCurrentWindow();
            DrawRenderableMarkerRows(popupMarkers);
            ImGui::EndChild();

            ImGui::End();
        }
    }

    bool linkPopupHovered = false;
    bool linkPopupBridgeHovered = false;
    if (mapLinkPopupState.open && mapLinkPopupState.tabId == tab.mapId) {
        auto targetTabIndexIt = mapTrackerState.tabIndexById.find(mapLinkPopupState.targetTabId);
        if (targetTabIndexIt == mapTrackerState.tabIndexById.end()) {
            CloseLinkPopup();
        } else {
            const int targetTabIndex = static_cast<int>(targetTabIndexIt->second);
            const MapTabData& targetTab = mapTrackerState.tabs[static_cast<size_t>(targetTabIndex)];
            const CachedMapTabRenderData& targetTabRenderData =
                renderCache.tabRenderDataByIndex[static_cast<size_t>(targetTabIndex)];
            std::optional<std::string> requirementSummary = std::nullopt;
            if (auto popupLinkIndex = FindMapLinkIndexByTargetTabId(tab, mapLinkPopupState.targetTabId);
                popupLinkIndex.has_value() && *popupLinkIndex < cachedTabRenderData.linkRequirementSummariesByIndex.size()) {
                requirementSummary = cachedTabRenderData.linkRequirementSummariesByIndex[*popupLinkIndex];
            }

            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(targetTabRenderData.renderableMarkers);
            const PopupWindowLayout popupLayout =
                ComputeClusterPopupLayout(popupMarkers, targetTabIndex, requirementSummary, currentViewport);
            mapLinkPopupState.popupPosition =
                ClampWindowPositionToViewport(mapLinkPopupState.popupPosition, popupLayout.windowSize, currentViewport);

            if (currentViewport != nullptr) {
                ImGui::SetNextWindowViewport(currentViewport->ID);
            }
            ImGui::SetNextWindowPos(mapLinkPopupState.popupPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(popupLayout.windowSize, ImGuiCond_Always);
            ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            std::string popupTitle = "Map Link##MapLinkPopup_" + tab.mapId + "_" + targetTab.mapId;
            ImGui::Begin(popupTitle.c_str(), nullptr, popupFlags);
            ApplyCheckTrackerFontScaleToCurrentWindow();
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            mapLinkPopupState.lastWindowPosition = ImGui::GetWindowPos();
            mapLinkPopupState.lastWindowSize = ImGui::GetWindowSize();
            mapLinkPopupState.hasLastWindowRect = true;
            linkPopupBridgeHovered = IsMouseInsidePopupBridgeRect(mapLinkPopupState.anchorMin, mapLinkPopupState.anchorMax,
                                                                  mapLinkPopupState.lastWindowPosition,
                                                                  mapLinkPopupState.lastWindowSize, mousePos);

            linkPopupHovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);

            DrawClusterPopupTargetHeader(targetTabIndex, requirementSummary);
            ImGui::BeginChild("MapLinkPopupRows", ImVec2(0.0f, popupLayout.childHeight), false,
                              ImGuiWindowFlags_NavFlattened);
            ApplyCheckTrackerFontScaleToCurrentWindow();
            DrawRenderableMarkerRows(popupMarkers);
            ImGui::EndChild();

            ImGui::End();
        }
    }

    if (mapClusterPopupState.open && !markerHoveredForPopup && !popupHovered && !popupBridgeHovered) {
        CloseClusterPopup();
    }
    if (mapLinkPopupState.open && !linkHoveredForPopup && !linkPopupHovered && !linkPopupBridgeHovered) {
        CloseLinkPopup();
    }

    FlushQueuedMarkerTooltip();
    ImGui::EndChild();
}

static bool DrawMapTrackerLoadingOrFatalState() {
    if (!mapTrackerState.attemptedLoad) {
        LoadMapTrackerData();
    }

    if (!mapTrackerState.fatalErrors.empty()) {
        for (const auto& fatalError : mapTrackerState.fatalErrors) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", fatalError.c_str());
        }
        ImGui::Separator();
        DrawMapTrackerIssuesTab();
        return true;
    }

    if (!mapTrackerState.loaded) {
        ImGui::TextUnformatted("Map tracker data is unavailable.");
        return true;
    }

    return false;
}

static bool ApplyRequestedMapTabSelection() {
    bool requestGroupTabSelection = false;

    if (!mapTrackerState.requestedTabId.empty()) {
        auto findIt = mapTrackerState.tabIndexById.find(mapTrackerState.requestedTabId);
        if (findIt != mapTrackerState.tabIndexById.end()) {
            mapTrackerState.selectedTabIndex = static_cast<int>(findIt->second);
            if (mapTrackerState.selectedTabIndex >= 0 &&
                mapTrackerState.selectedTabIndex < static_cast<int>(mapTrackerState.tabs.size())) {
                mapTrackerState.selectedGroupName = mapTrackerState.tabs[mapTrackerState.selectedTabIndex].groupName;
                requestGroupTabSelection = true;
            }
        }
    }
    mapTrackerState.requestedTabId.clear();

    return requestGroupTabSelection;
}

static void ResolveMapGroupState(bool showIssuesTab, const char* issuesGroupName, const char* debugNoGroupName,
                                 bool& outShowDebugFallbackGroup, bool& outShowGroupTabs) {
    bool hasNoNamedGroups = mapTrackerState.mapGroups.empty();
    outShowDebugFallbackGroup = hasNoNamedGroups && showIssuesTab;
    outShowGroupTabs = mapTrackerState.mapGroups.size() > 1 || outShowDebugFallbackGroup;

    if (outShowGroupTabs) {
        if (outShowDebugFallbackGroup) {
            if (mapTrackerState.selectedGroupName.empty() ||
                (mapTrackerState.selectedGroupName != issuesGroupName &&
                 mapTrackerState.selectedGroupName != debugNoGroupName)) {
                mapTrackerState.selectedGroupName = debugNoGroupName;
            }
        } else if (mapTrackerState.selectedGroupName.empty() ||
                   !mapTrackerState.tabIndicesByGroup.contains(mapTrackerState.selectedGroupName)) {
            mapTrackerState.selectedGroupName = mapTrackerState.mapGroups.front();
        }
    } else if (!showIssuesTab && mapTrackerState.selectedGroupName == issuesGroupName) {
        mapTrackerState.selectedGroupName.clear();
    }
}

static void ClampSelectedMapTabIndex() {
    int maxSelectableTabIndex = std::max(0, static_cast<int>(mapTrackerState.tabs.size()) - 1);
    mapTrackerState.selectedTabIndex = std::clamp(mapTrackerState.selectedTabIndex, 0, maxSelectableTabIndex);
}

static void DrawMapGroupTabs(bool showGroupTabs, bool showDebugFallbackGroup, bool showIssuesTab,
                             bool requestGroupTabSelection, const char* issuesGroupName, const char* debugNoGroupName) {
    if (!showGroupTabs) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Tab, IM_COL32(34, 74, 160, 235));
    ImGui::PushStyleColor(ImGuiCol_TabActive, IM_COL32(56, 118, 230, 255));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, IM_COL32(80, 145, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, IM_COL32(32, 58, 120, 210));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, IM_COL32(43, 88, 176, 230));
    const std::string groupSelectionForUi = mapTrackerState.selectedGroupName;
    if (ImGui::BeginTabBar("CheckTrackerMapGroups",
                           ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        if (showDebugFallbackGroup) {
            ImGuiTabItemFlags groupTabFlags =
                requestGroupTabSelection && groupSelectionForUi == debugNoGroupName ? ImGuiTabItemFlags_SetSelected
                                                                                    : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(debugNoGroupName, nullptr, groupTabFlags)) {
                bool useThisTabSelection = !requestGroupTabSelection || groupSelectionForUi == debugNoGroupName ||
                                           ImGui::IsItemActivated();
                if (useThisTabSelection) {
                    mapTrackerState.selectedGroupName = debugNoGroupName;
                }
                ImGui::EndTabItem();
            }
        }
        for (const auto& groupName : mapTrackerState.mapGroups) {
            ImGuiTabItemFlags groupTabFlags =
                requestGroupTabSelection && groupSelectionForUi == groupName ? ImGuiTabItemFlags_SetSelected
                                                                             : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(groupName.c_str(), nullptr, groupTabFlags)) {
                bool useThisTabSelection =
                    !requestGroupTabSelection || groupSelectionForUi == groupName || ImGui::IsItemActivated();
                if (useThisTabSelection) {
                    mapTrackerState.selectedGroupName = groupName;
                }
                ImGui::EndTabItem();
            }
        }
        if (showIssuesTab) {
            ImGuiTabItemFlags issuesTabFlags =
                requestGroupTabSelection && groupSelectionForUi == issuesGroupName ? ImGuiTabItemFlags_SetSelected
                                                                                   : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(issuesGroupName, nullptr, issuesTabFlags)) {
                bool useThisTabSelection = !requestGroupTabSelection || groupSelectionForUi == issuesGroupName ||
                                           ImGui::IsItemActivated();
                if (useThisTabSelection) {
                    mapTrackerState.selectedGroupName = issuesGroupName;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(5);
}

static std::vector<int> BuildVisibleMapTabIndices(bool showingIssuesTab, bool showGroupTabs, bool showDebugFallbackGroup,
                                                  const char* debugNoGroupName) {
    std::vector<int> visibleTabIndices;
    if (showingIssuesTab) {
        return visibleTabIndices;
    }

    if (showDebugFallbackGroup && mapTrackerState.selectedGroupName == debugNoGroupName) {
        for (int tabIndex = 0; tabIndex < static_cast<int>(mapTrackerState.tabs.size()); tabIndex++) {
            visibleTabIndices.push_back(tabIndex);
        }
    } else if (showGroupTabs && mapTrackerState.tabIndicesByGroup.contains(mapTrackerState.selectedGroupName)) {
        visibleTabIndices = mapTrackerState.tabIndicesByGroup[mapTrackerState.selectedGroupName];
    } else {
        for (int tabIndex = 0; tabIndex < static_cast<int>(mapTrackerState.tabs.size()); tabIndex++) {
            visibleTabIndices.push_back(tabIndex);
        }
    }

    if (!visibleTabIndices.empty()) {
        bool selectedTabVisible =
            std::find(visibleTabIndices.begin(), visibleTabIndices.end(), mapTrackerState.selectedTabIndex) !=
            visibleTabIndices.end();
        if (!selectedTabVisible) {
            if (showGroupTabs) {
                auto rememberedTabIndexIt = mapTrackerState.lastSelectedTabByGroup.find(mapTrackerState.selectedGroupName);
                if (rememberedTabIndexIt != mapTrackerState.lastSelectedTabByGroup.end() &&
                    std::find(visibleTabIndices.begin(), visibleTabIndices.end(), rememberedTabIndexIt->second) !=
                        visibleTabIndices.end()) {
                    mapTrackerState.selectedTabIndex = rememberedTabIndexIt->second;
                } else {
                    mapTrackerState.selectedTabIndex = visibleTabIndices.front();
                }
            } else {
                mapTrackerState.selectedTabIndex = visibleTabIndices.front();
            }
        }

        if (showGroupTabs) {
            mapTrackerState.lastSelectedTabByGroup[mapTrackerState.selectedGroupName] = mapTrackerState.selectedTabIndex;
        }
    }

    bool hasSingleVisibleMap = visibleTabIndices.size() == 1;
    if (hasSingleVisibleMap) {
        mapTrackerState.selectedTabIndex = visibleTabIndices.front();
        if (showGroupTabs) {
            mapTrackerState.lastSelectedTabByGroup[mapTrackerState.selectedGroupName] = mapTrackerState.selectedTabIndex;
        }
    }

    return visibleTabIndices;
}

static void DrawMapTrackerTabButtons(const std::vector<int>& visibleTabIndices, bool showGroupTabs,
                                     const std::vector<MapTabVisualSummary>& tabVisualSummaries,
                                     const ImVec4& selectedTabColor) {
    float tabsRowStartX = ImGui::GetCursorPosX();
    float tabsRowMaxX = tabsRowStartX + ImGui::GetContentRegionAvail().x;
    bool hasPreviousTabButton = false;
    const ImVec2 compactMapButtonPadding =
        ImVec2(std::max(2.0f, ImGui::GetStyle().FramePadding.x * 0.78f),
               std::max(2.0f, ImGui::GetStyle().FramePadding.y * 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compactMapButtonPadding);
    auto drawTabButton = [&](const std::string& label, int tabIndex, const std::optional<ImVec4>& baseColor) {
        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        float buttonWidth = textSize.x + (ImGui::GetStyle().FramePadding.x * 2.0f) + 6.0f;
        buttonWidth = std::min(buttonWidth, std::max(1.0f, tabsRowMaxX - tabsRowStartX));
        if (hasPreviousTabButton) {
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + buttonWidth > tabsRowMaxX) {
                ImGui::NewLine();
            }
        }
        bool isSelected = (mapTrackerState.selectedTabIndex == tabIndex);
        if (baseColor.has_value()) {
            ImVec4 buttonColor = *baseColor;
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ScaleMapTabColor(buttonColor, 1.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ScaleMapTabColor(buttonColor, 0.9f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, isSelected ? selectedTabColor : ScaleMapTabColor(buttonColor, 0.72f));
        } else if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, selectedTabColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selectedTabColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, selectedTabColor);
        }
        if (ImGui::Button(label.c_str(), ImVec2(buttonWidth, 0.0f))) {
            mapTrackerState.selectedTabIndex = tabIndex;
            if (showGroupTabs) {
                mapTrackerState.lastSelectedTabByGroup[mapTrackerState.selectedGroupName] = tabIndex;
            }
        }
        if (isSelected) {
            ImDrawList* tabDrawList = ImGui::GetWindowDrawList();
            ImVec2 rectMin = ImGui::GetItemRectMin();
            ImVec2 rectMax = ImGui::GetItemRectMax();
            float rounding = ImGui::GetStyle().FrameRounding;
            tabDrawList->AddRect(rectMin, rectMax, IM_COL32(255, 255, 255, 255), rounding, 0, 2.0f);
            tabDrawList->AddRect(ImVec2(rectMin.x + 1.0f, rectMin.y + 1.0f), ImVec2(rectMax.x - 1.0f, rectMax.y - 1.0f),
                                 IM_COL32(15, 15, 15, 220), rounding, 0, 1.0f);
        }
        if (baseColor.has_value()) {
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
        } else if (isSelected) {
            ImGui::PopStyleColor(3);
        }
        hasPreviousTabButton = true;
    };

    for (const int tabIndex : visibleTabIndices) {
        ImVec4 mapTabColor = GetMapTabBaseColor(tabVisualSummaries[static_cast<size_t>(tabIndex)]);
        drawTabButton(mapTrackerState.tabs[static_cast<size_t>(tabIndex)].mapName, tabIndex, mapTabColor);
    }
    ImGui::PopStyleVar();
}

void DrawMapTrackerContent() {
    if (DrawMapTrackerLoadingOrFatalState()) {
        return;
    }

    UpdateRequestedMapTabFromCurrentArea(false);
    bool mqSpoilers = CVarGetInteger(CVAR_TRACKER_CHECK("MQSpoilers"), 0);
    constexpr const char* issuesGroupName = "Unlinked / Issues";
    constexpr const char* debugNoGroupName = "Others";

    bool showIssuesTab = showMapDebugDetails;
    bool requestGroupTabSelection = ApplyRequestedMapTabSelection();
    bool showDebugFallbackGroup = false;
    bool showGroupTabs = false;
    ResolveMapGroupState(showIssuesTab, issuesGroupName, debugNoGroupName, showDebugFallbackGroup, showGroupTabs);
    ClampSelectedMapTabIndex();

    ImVec4 selectedTabColor = ImGui::ColorConvertU32ToFloat4(THEME_COLOR);
    const MapTrackerRenderCache& renderCache = GetMapTrackerRenderCache(mqSpoilers);
    const std::vector<MapTabVisualSummary>& tabVisualSummaries = renderCache.tabVisualSummaries;

    DrawMapGroupTabs(showGroupTabs, showDebugFallbackGroup, showIssuesTab, requestGroupTabSelection, issuesGroupName,
                     debugNoGroupName);

    bool showingIssuesTab = showGroupTabs && showIssuesTab && (mapTrackerState.selectedGroupName == issuesGroupName);
    std::vector<int> visibleTabIndices =
        BuildVisibleMapTabIndices(showingIssuesTab, showGroupTabs, showDebugFallbackGroup, debugNoGroupName);

    bool showMapButtons = !showingIssuesTab && visibleTabIndices.size() > 1;
    if (showMapButtons) {
        DrawMapTrackerTabButtons(visibleTabIndices, showGroupTabs, tabVisualSummaries, selectedTabColor);
    }

    ImGui::Separator();
    ImVec2 mapBodySize = ImGui::GetContentRegionAvail();
    mapBodySize.y = std::max(1.0f, mapBodySize.y - ImGui::GetStyle().ItemSpacing.y);

    ImGuiWindowFlags mapBodyFlags = ImGuiWindowFlags_None;
    if (!showingIssuesTab) {
        mapBodyFlags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    if (ImGui::BeginChild("CheckTrackerMapBody", mapBodySize, false, mapBodyFlags)) {
        if (showingIssuesTab) {
            mapTrackerState.lastMapViewTabIndex = -1;
            DrawMapTrackerIssuesTab();
        } else if (!mapTrackerState.tabs.empty() &&
                   mapTrackerState.selectedTabIndex >= 0 &&
                   mapTrackerState.selectedTabIndex < static_cast<int>(mapTrackerState.tabs.size())) {
            if (mapTrackerState.lastMapViewTabIndex != mapTrackerState.selectedTabIndex) {
                MapTabData& selectedMapTab = mapTrackerState.tabs[mapTrackerState.selectedTabIndex];
                selectedMapTab.zoomFactor = 1.0f;
                selectedMapTab.panOffset = ImVec2(0.0f, 0.0f);
                mapTrackerState.lastMapViewTabIndex = mapTrackerState.selectedTabIndex;
            }
            DrawMapTabContent(mapTrackerState.selectedTabIndex, renderCache);
        } else {
            mapTrackerState.lastMapViewTabIndex = -1;
            ImGui::TextUnformatted("No map tabs available for this group.");
        }
    }
    ImGui::EndChild();
}

} // namespace CheckTracker
