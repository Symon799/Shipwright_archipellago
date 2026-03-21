#include "map_tracker_internal.h"
#include "soh/OTRGlobals.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/util.h"

#include <algorithm>
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
            ImGui::TextUnformatted(issue.summary.c_str());
            if (!issue.details.empty()) {
                ImGui::TextDisabled("%s", issue.details.c_str());
            }
            if (issueIndex + 1 < issues.size()) {
                ImGui::Separator();
            }
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
                ImGui::TextUnformatted(GetCheckDisplayName(rc).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", GetGameCheckMapTrackerId(rc).c_str());
            }
        }
    }

    if (mapTrackerState.warnings.empty() && mapTrackerState.unresolvedLinks.empty() &&
        mapTrackerState.unassignedCheckIds.empty()) {
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
    double keepAliveUntil = 0.0;
};

static ClusterPopupState mapClusterPopupState;

struct LinkPopupState {
    bool open = false;
    std::string tabId;
    std::string targetTabId;
    ImVec2 popupPosition = { 0.0f, 0.0f };
    double keepAliveUntil = 0.0;
};

static LinkPopupState mapLinkPopupState;

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
    ImVec2 windowSize = { 0.0f, 0.0f };
    float contentWidth = 0.0f;
};

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
        mapClusterPopupState.open = false;
        mapLinkPopupState.open = false;
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

static MarkerTooltipLayout ComputeMarkerTooltipLayout(const MarkerTooltipContent& content) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineHeight = ImGui::GetTextLineHeight();

    float maxWindowWidth = 900.0f;
    float maxWindowHeight = 760.0f;
    if (const ImGuiViewport* viewport = ImGui::GetMainViewport(); viewport != nullptr) {
        maxWindowWidth = std::max(CHECK_TRACKER_MAP_TOOLTIP_MIN_CONTENT_WIDTH + (style.WindowPadding.x * 2.0f),
                                  viewport->WorkSize.x * CHECK_TRACKER_MAP_TOOLTIP_MAX_VIEWPORT_WIDTH_RATIO);
        maxWindowHeight = std::max(120.0f, viewport->WorkSize.y * CHECK_TRACKER_MAP_TOOLTIP_MAX_VIEWPORT_HEIGHT_RATIO);
    }
    float maxContentWidth = std::max(CHECK_TRACKER_MAP_TOOLTIP_MIN_CONTENT_WIDTH,
                                     maxWindowWidth - (style.WindowPadding.x * 2.0f) - 4.0f);

    float contentWidth = std::max(CHECK_TRACKER_MAP_TOOLTIP_MIN_CONTENT_WIDTH,
                                  ImGui::CalcTextSize(content.checkName.c_str()).x);
    if (!content.requirementSummary.empty()) {
        contentWidth = std::max(contentWidth, ImGui::CalcTextSize(content.requirementSummary.c_str()).x);
    }
    if (!content.extraText.empty()) {
        std::string extraLabel = fmt::format("({})", content.extraText);
        contentWidth = std::max(contentWidth, ImGui::CalcTextSize(extraLabel.c_str()).x);
    }
    if (!content.hintText.empty()) {
        contentWidth = std::max(contentWidth, ImGui::CalcTextSize("Hint: ").x + CHECK_TRACKER_MAP_TOOLTIP_MIN_CONTENT_WIDTH);
    } else if (content.showHintPrompt) {
        contentWidth = std::max(contentWidth, ImGui::CalcTextSize("Right click to show hint").x);
    }
    if (!content.logicBranches.empty()) {
        contentWidth = std::max(contentWidth, CHECK_TRACKER_MAP_TOOLTIP_LOGIC_MIN_CONTENT_WIDTH);
    }
    if (!content.checkMapTrackerId.empty()) {
        contentWidth = std::max(
            contentWidth, ImGui::CalcTextSize(fmt::format("Tracker ID: {}", content.checkMapTrackerId).c_str()).x);
    }
    if (!content.packCheckName.empty()) {
        contentWidth =
            std::max(contentWidth, ImGui::CalcTextSize(fmt::format("Pack: {}", content.packCheckName).c_str()).x);
    }
    contentWidth = std::clamp(contentWidth, CHECK_TRACKER_MAP_TOOLTIP_MIN_CONTENT_WIDTH, maxContentWidth);

    float contentHeight = lineHeight;
    bool hasDetailsSection = false;
    if (!content.requirementSummary.empty()) {
        contentHeight += style.ItemSpacing.y + lineHeight;
        hasDetailsSection = true;
    }
    if (!content.extraText.empty()) {
        std::string extraLabel = fmt::format("({})", content.extraText);
        float extraHeight = ImGui::CalcTextSize(extraLabel.c_str(), nullptr, false, contentWidth).y;
        contentHeight += style.ItemSpacing.y + std::max(lineHeight, extraHeight);
        hasDetailsSection = true;
    }
    if (!content.hintText.empty() || content.showHintPrompt) {
        contentHeight += (style.ItemSpacing.y * 2.0f) + 2.0f;
        const std::string hintLabel =
            content.hintText.empty() ? "Right click to show hint." : fmt::format("Hint: {}", content.hintText);
        float hintHeight = ImGui::CalcTextSize(hintLabel.c_str(), nullptr, false, contentWidth).y;
        contentHeight += std::max(lineHeight, hintHeight);
        hasDetailsSection = true;
    }
    if (!content.logicBranches.empty()) {
        contentHeight += (style.ItemSpacing.y * 2.0f) + 2.0f;
        for (size_t branchIndex = 0; branchIndex < content.logicBranches.size(); branchIndex++) {
            float logicHeight = ImGui::CalcTextSize(content.logicBranches[branchIndex].c_str(), nullptr, false, contentWidth).y;
            contentHeight += std::max(lineHeight, logicHeight);
            if (branchIndex + 1 < content.logicBranches.size()) {
                contentHeight += (style.ItemSpacing.y * 2.0f) + lineHeight;
            }
        }
        hasDetailsSection = true;
    }
    if (!content.checkMapTrackerId.empty()) {
        if (hasDetailsSection) {
            contentHeight += (style.ItemSpacing.y * 2.0f) + 2.0f;
        }
        contentHeight += lineHeight;
        if (!content.packCheckName.empty()) {
            contentHeight += style.ItemSpacing.y + lineHeight;
        }
    }

    MarkerTooltipLayout layout;
    layout.contentWidth = contentWidth;
    layout.windowSize.x = contentWidth + (style.WindowPadding.x * 2.0f) + 4.0f;
    layout.windowSize.y = contentHeight + (style.WindowPadding.y * 2.0f) + 2.0f;
    layout.windowSize.y = std::min(layout.windowSize.y, maxWindowHeight);
    return layout;
}

static void DrawMarkerTooltip(const MarkerTooltipContent& content) {
    MarkerTooltipLayout layout = ComputeMarkerTooltipLayout(content);
    ImGui::SetNextWindowSize(layout.windowSize, ImGuiCond_Always);
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(content.checkName.c_str());

    bool hasTooltipDetails = false;
    if (!content.requirementSummary.empty()) {
        ImGui::TextDisabled("%s", content.requirementSummary.c_str());
        hasTooltipDetails = true;
    }

    if (!content.extraText.empty()) {
        std::string extraLabel = fmt::format("({})", content.extraText);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(content.extraColor.r / 255.0f, content.extraColor.g / 255.0f,
                                     content.extraColor.b / 255.0f, content.extraColor.a / 255.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.contentWidth);
        ImGui::TextUnformatted(extraLabel.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        hasTooltipDetails = true;
    }

    if (!content.hintText.empty() || content.showHintPrompt) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.contentWidth);
        if (!content.hintText.empty()) {
            std::string hintLabel = fmt::format("Hint: {}", content.hintText);
            ImGui::TextUnformatted(hintLabel.c_str());
        } else {
            ImGui::TextDisabled("%s", "Right click to show hint.");
        }
        ImGui::PopTextWrapPos();
        hasTooltipDetails = true;
    }

    if (!content.logicBranches.empty()) {
        ImGui::Separator();
        for (size_t branchIndex = 0; branchIndex < content.logicBranches.size(); branchIndex++) {
            if (branchIndex > 0) {
                ImGui::TextDisabled("%s", "----- OR -----");
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.contentWidth);
            ImGui::TextUnformatted(content.logicBranches[branchIndex].c_str());
            ImGui::PopTextWrapPos();
        }
        hasTooltipDetails = true;
    }

    if (!content.checkMapTrackerId.empty()) {
        if (hasTooltipDetails) {
            ImGui::Separator();
        }
        ImGui::TextDisabled("Tracker ID: %s", content.checkMapTrackerId.c_str());
        if (!content.packCheckName.empty()) {
            ImGui::TextDisabled("Pack: %s", content.packCheckName.c_str());
        }
    }

    ImGui::EndTooltip();
}

static void DrawRenderableMapMarkerTooltip(const RenderableMapMarker& renderableMarker) {
    DrawMarkerTooltip(BuildMarkerTooltipContent(renderableMarker.marker->check, renderableMarker.marker->packCheckName));
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
    ImVec2 mapNameStart = ImGui::GetCursorScreenPos();
    ImGui::TextUnformatted(popupTargetTab.mapName.c_str());
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(mapNameStart.x + 0.9f, mapNameStart.y),
                                        ImGui::GetColorU32(ImGuiCol_Text), popupTargetTab.mapName.c_str());
    if (requirementSummary.has_value() && !requirementSummary->empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", requirementSummary->c_str());
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

static ImVec2 ComputeClusterPopupWindowSize(const std::vector<const RenderableMapMarker*>& clusterMarkers,
                                            const std::optional<int>& popupNavigationTargetTabIndex,
                                            const std::optional<std::string>& requirementSummary = std::nullopt) {
    const ImGuiStyle& popupStyle = ImGui::GetStyle();
    float popupTextLineHeight = ImGui::GetTextLineHeight();
    float popupRowHeight = std::max(14.0f, popupTextLineHeight + 1.0f);
    float popupStatusWidth = std::max(10.0f, popupRowHeight - 2.0f);

    float measuredContentWidth = 0.0f;
    float measuredContentHeight = 0.0f;

    if (popupNavigationTargetTabIndex.has_value() && *popupNavigationTargetTabIndex >= 0 &&
        *popupNavigationTargetTabIndex < static_cast<int>(mapTrackerState.tabs.size())) {
        const MapTabData& popupTargetTab = mapTrackerState.tabs[static_cast<size_t>(*popupNavigationTargetTabIndex)];
        float headerWidth = ImGui::CalcTextSize(popupTargetTab.mapName.c_str()).x + 1.0f;
        if (requirementSummary.has_value() && !requirementSummary->empty()) {
            headerWidth += ImGui::CalcTextSize((" (" + *requirementSummary + ")").c_str()).x;
        }
        measuredContentWidth = std::max(measuredContentWidth, headerWidth);
        measuredContentHeight += popupTextLineHeight;
        measuredContentHeight += (popupStyle.ItemSpacing.y * 2.0f) + 2.0f;
    }

    for (size_t clusterIndex = 0; clusterIndex < clusterMarkers.size(); clusterIndex++) {
        const RenderableMapMarker& renderableMarker = *clusterMarkers[clusterIndex];
        const MapMarker& marker = *renderableMarker.marker;

        std::string checkName = GetCheckDisplayName(marker.check);
        float selectableWidth = ImGui::CalcTextSize(checkName.c_str()).x + (popupStyle.FramePadding.x * 2.0f);
        float rowWidth = popupStatusWidth + 6.0f + selectableWidth;

        std::string extraText = GetCheckExtraInfoText(marker.check);
        if (!extraText.empty()) {
            std::string extraLabel = fmt::format("({})", extraText);
            rowWidth += popupStyle.ItemSpacing.x + ImGui::CalcTextSize(extraLabel.c_str()).x;
        }

        measuredContentWidth = std::max(measuredContentWidth, rowWidth);
        measuredContentHeight += popupRowHeight;
        if (clusterIndex + 1 < clusterMarkers.size()) {
            measuredContentHeight += popupStyle.ItemSpacing.y;
        }
    }

    ImVec2 popupWindowSize(measuredContentWidth + (popupStyle.WindowPadding.x * 2.0f) + 4.0f,
                           measuredContentHeight + (popupStyle.WindowPadding.y * 2.0f) + 2.0f);
    if (const ImGuiViewport* viewport = ImGui::GetMainViewport(); viewport != nullptr) {
        bool hasHeader = popupNavigationTargetTabIndex.has_value();
        float minPopupWidth = hasHeader ? 180.0f : 96.0f;
        float minPopupHeight = hasHeader ? 90.0f : 24.0f;
        popupWindowSize.x =
            std::clamp(popupWindowSize.x, minPopupWidth, std::max(minPopupWidth, viewport->WorkSize.x * 0.55f));
        popupWindowSize.y =
            std::clamp(popupWindowSize.y, minPopupHeight, std::max(minPopupHeight, viewport->WorkSize.y * 0.75f));
    }
    return popupWindowSize;
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
    for (size_t markerIndex = 0; markerIndex < renderableMarkers.size(); markerIndex++) {
        const RenderableMapMarker& renderableMarker = *renderableMarkers[markerIndex];
        const MapMarker& marker = *renderableMarker.marker;
        bool canToggle = CanToggleSkippedStateForCheck(marker.check);
        std::string checkName = GetCheckDisplayName(marker.check);
        std::string markerRowId = fmt::format("PopupCheckRow_{}_{}_{}_{}", static_cast<int>(marker.check), markerIndex,
                                              marker.packCheckName, marker.mapId);
        std::string checkSelectableLabel = checkName + "##Select";

        ImGui::PushID(markerRowId.c_str());
        float popupRowHeight = std::max(14.0f, ImGui::GetTextLineHeight() + 1.0f);

        if (!canToggle) {
            ImGui::BeginDisabled();
        }

        ImVec2 statusSize(std::max(10.0f, popupRowHeight - 2.0f), std::max(10.0f, popupRowHeight - 2.0f));
        bool statusPressed = ImGui::ColorButton("##Status", ImGui::ColorConvertU32ToFloat4(renderableMarker.fillColor),
                                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                                statusSize);
        bool statusHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::SameLine(0.0f, 6.0f);

        bool selected = false;
        float selectableWidth = ImGui::CalcTextSize(checkName.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2.0f);
        bool rowPressed = ImGui::Selectable(checkSelectableLabel.c_str(), &selected, ImGuiSelectableFlags_AllowDoubleClick,
                                            ImVec2(selectableWidth, popupRowHeight));
        bool rowHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        if ((statusPressed || rowPressed) && canToggle) {
            ToggleSkippedStateForCheck(marker.check);
            mapClusterPopupState.keepAliveUntil = std::max(mapClusterPopupState.keepAliveUntil, ImGui::GetTime() + 0.16);
            mapLinkPopupState.keepAliveUntil = std::max(mapLinkPopupState.keepAliveUntil, ImGui::GetTime() + 0.16);
        }

        if (!canToggle) {
            ImGui::EndDisabled();
        }

        std::string extraText = GetCheckExtraInfoText(marker.check);
        bool extraHovered = false;
        if (!extraText.empty()) {
            ImGui::SameLine();
            Color_RGBA8 legacyExtraColor = GetLegacyCheckExtraColor(marker.check);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImVec4(legacyExtraColor.r / 255.0f, legacyExtraColor.g / 255.0f, legacyExtraColor.b / 255.0f,
                       legacyExtraColor.a / 255.0f));
            ImGui::Text("(%s)", extraText.c_str());
            ImGui::PopStyleColor();
            extraHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        }

        bool rowTooltipHovered = statusHovered || rowHovered || extraHovered;
        bool hintTogglePressed = rowTooltipHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                                 !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
        if (rowTooltipHovered) {
            mapClusterPopupState.keepAliveUntil = std::max(mapClusterPopupState.keepAliveUntil, ImGui::GetTime() + 0.16);
            mapLinkPopupState.keepAliveUntil = std::max(mapLinkPopupState.keepAliveUntil, ImGui::GetTime() + 0.16);
            if (hintTogglePressed) {
                ToggleMapTrackerCheckHint(marker.check);
            }
            DrawMarkerTooltip(BuildMarkerTooltipContent(marker.check, marker.packCheckName));
        }

        ImGui::PopID();
    }
}

void DrawMapTabContent(int tabIndex, const MapTrackerRenderCache& renderCache) {
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
        mapClusterPopupState.open = false;
        mapClusterPopupState.tabId = tab.mapId;
        mapClusterPopupState.stackKey.clear();
        mapClusterPopupState.keepAliveUntil = 0.0;
    }
    if (mapLinkPopupState.tabId != tab.mapId) {
        mapLinkPopupState.open = false;
        mapLinkPopupState.tabId = tab.mapId;
        mapLinkPopupState.targetTabId.clear();
        mapLinkPopupState.keepAliveUntil = 0.0;
    }

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseOverPopupWindow = false;

    if (mapClusterPopupState.open && mapClusterPopupState.tabId == tab.mapId) {
        auto popupClusterIt = cachedTabRenderData.renderableMarkersByStackKey.find(mapClusterPopupState.stackKey);
        if (popupClusterIt != cachedTabRenderData.renderableMarkersByStackKey.end() && !popupClusterIt->second.empty()) {
            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(popupClusterIt->second);
            if (!popupMarkers.empty()) {
                const ImVec2 popupWindowSize = ComputeClusterPopupWindowSize(popupMarkers, std::nullopt);
                mouseOverPopupWindow |=
                    IsMouseInsidePopupWindowRect(mapClusterPopupState.popupPosition, popupWindowSize, mousePos);
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
                const ImVec2 popupWindowSize =
                    ComputeClusterPopupWindowSize(popupMarkers, targetTabIndex, requirementSummary);
                mouseOverPopupWindow |=
                    IsMouseInsidePopupWindowRect(mapLinkPopupState.popupPosition, popupWindowSize, mousePos);
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
    double nowTime = ImGui::GetTime();

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
                mapLinkPopupState.open = false;
                mapClusterPopupState.open = true;
                mapClusterPopupState.tabId = tab.mapId;
                mapClusterPopupState.stackKey = stackKey;
                mapClusterPopupState.popupPosition = ImVec2(markerMax.x + 10.0f, markerMin.y - 4.0f);
                mapClusterPopupState.keepAliveUntil = nowTime + 0.16;
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
            DrawRenderableMapMarkerTooltip(renderableMarkers.front());
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
            mapClusterPopupState.open = false;
            mapLinkPopupState.open = false;
            NavigateToMapTab(targetTabIndex);
            continue;
        }

        if (hovered) {
            linkHoveredForPopup = true;
            mapClusterPopupState.open = false;
            mapLinkPopupState.open = true;
            mapLinkPopupState.tabId = tab.mapId;
            mapLinkPopupState.targetTabId = link.targetMapId;
            mapLinkPopupState.popupPosition = ImVec2(markerMax.x + 10.0f, markerMin.y - 4.0f);
            mapLinkPopupState.keepAliveUntil = nowTime + 0.16;
        }
    }

    bool popupHovered = false;
    if (mapClusterPopupState.open && mapClusterPopupState.tabId == tab.mapId) {
        auto popupClusterIt = cachedTabRenderData.renderableMarkersByStackKey.find(mapClusterPopupState.stackKey);
        if (popupClusterIt == cachedTabRenderData.renderableMarkersByStackKey.end() || popupClusterIt->second.empty()) {
            mapClusterPopupState.open = false;
        } else {
            const auto popupMarkers = BuildPopupRenderableMarkersWithoutDuplicateChecks(popupClusterIt->second);
            const ImVec2 popupWindowSize = ComputeClusterPopupWindowSize(popupMarkers, std::nullopt);

            ImGui::SetNextWindowPos(mapClusterPopupState.popupPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(popupWindowSize, ImGuiCond_Always);
            ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav;
            std::string popupTitle = "Map Checks##MapClusterPopup_" + tab.mapId;
            ImGui::Begin(popupTitle.c_str(), nullptr, popupFlags);

            popupHovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);
            if (popupHovered) {
                mapClusterPopupState.keepAliveUntil = std::max(mapClusterPopupState.keepAliveUntil, nowTime + 0.16);
            }

            DrawRenderableMarkerRows(popupMarkers);

            ImGui::End();
        }
    }

    bool linkPopupHovered = false;
    if (mapLinkPopupState.open && mapLinkPopupState.tabId == tab.mapId) {
        auto targetTabIndexIt = mapTrackerState.tabIndexById.find(mapLinkPopupState.targetTabId);
        if (targetTabIndexIt == mapTrackerState.tabIndexById.end()) {
            mapLinkPopupState.open = false;
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
            const ImVec2 popupWindowSize = ComputeClusterPopupWindowSize(popupMarkers, targetTabIndex, requirementSummary);

            ImGui::SetNextWindowPos(mapLinkPopupState.popupPosition, ImGuiCond_Always);
            ImGui::SetNextWindowSize(popupWindowSize, ImGuiCond_Always);
            ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav;
            std::string popupTitle = "Map Link##MapLinkPopup_" + tab.mapId + "_" + targetTab.mapId;
            ImGui::Begin(popupTitle.c_str(), nullptr, popupFlags);

            linkPopupHovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);
            if (linkPopupHovered) {
                mapLinkPopupState.keepAliveUntil = std::max(mapLinkPopupState.keepAliveUntil, nowTime + 0.16);
            }

            DrawClusterPopupTargetHeader(targetTabIndex, requirementSummary);
            DrawRenderableMarkerRows(popupMarkers);

            ImGui::End();
        }
    }

    if (mapClusterPopupState.open && !markerHoveredForPopup && !popupHovered &&
        ImGui::GetTime() > mapClusterPopupState.keepAliveUntil) {
        mapClusterPopupState.open = false;
    }
    if (mapLinkPopupState.open && !linkHoveredForPopup && !linkPopupHovered &&
        ImGui::GetTime() > mapLinkPopupState.keepAliveUntil) {
        mapLinkPopupState.open = false;
    }

    ImGui::EndChild();
}

static bool DrawMapTrackerLoadingOrFatalState() {
    if (!mapTrackerState.attemptedLoad) {
        SPDLOG_INFO("[CheckTrackerMapDiag] First map render requested load.");
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
