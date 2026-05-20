#include "check_tracker_visibility_debug.h"

#include "map_tracker_internal.h"
#include "randomizer_check_ids.h"
#include "randomizer_check_tracker.h"
#include "static_data.h"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"

#ifdef IS_ARCHIPELAGO
#include "soh/Network/Archipelago/Archipelago.h"
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>
#include <unordered_set>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "variables.h"
}

namespace CheckTracker {
extern bool showKeysanity;
extern bool showBossKeysanity;
extern std::map<RandomizerCheckArea, std::vector<RandomizerCheck>> checksByArea;
} // namespace CheckTracker

namespace CheckTracker {
namespace {

constexpr const char* CHECK_TRACKER_VISIBILITY_DEBUG_CVAR = CVAR_TRACKER_CHECK("VisibilityDebugDump");

const char* QuestLabel(RandomizerCheckQuest quest) {
    switch (quest) {
        case RCQUEST_VANILLA:
            return "vanilla";
        case RCQUEST_MQ:
            return "mq";
        case RCQUEST_BOTH:
            return "both";
        default:
            return "unknown";
    }
}

const char* RCTypeLabel(RandomizerCheckType type) {
    switch (type) {
        case RCTYPE_SMALL_KEY:
            return "SMALL_KEY";
        case RCTYPE_BOSS_KEY:
            return "BOSS_KEY";
        case RCTYPE_STANDARD:
            return "STANDARD";
        case RCTYPE_MAP:
            return "MAP";
        case RCTYPE_COMPASS:
            return "COMPASS";
        default:
            return "OTHER";
    }
}

std::string ItemName(RandomizerGet rg) {
    if (rg == RG_NONE) {
        return "(none)";
    }
    return Rando::StaticData::RetrieveItem(rg).GetName().GetEnglish();
}

std::string KeysanitySettingLabel() {
    if (!IS_RANDO) {
        return "n/a (not rando)";
    }
    const uint8_t value = OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_KEYSANITY);
    switch (value) {
        case RO_DUNGEON_ITEM_LOC_STARTWITH:
            return "Start With";
        case RO_DUNGEON_ITEM_LOC_VANILLA:
            return "Vanilla";
        case RO_DUNGEON_ITEM_LOC_OWN_DUNGEON:
            return "Own Dungeon";
        case RO_DUNGEON_ITEM_LOC_ANY_DUNGEON:
            return "Any Dungeon";
        case RO_DUNGEON_ITEM_LOC_OVERWORLD:
            return "Overworld";
        case RO_DUNGEON_ITEM_LOC_ANYWHERE:
            return "Anywhere";
        default:
            return fmt::format("Unknown({})", value);
    }
}

bool IsInChecksByArea(RandomizerCheck rc, RandomizerCheckArea area) {
    auto areaIt = checksByArea.find(area);
    if (areaIt == checksByArea.end()) {
        return false;
    }
    const auto& checks = areaIt->second;
    return std::find(checks.begin(), checks.end(), rc) != checks.end();
}

std::vector<std::string> CollectSortedSohIds(const std::unordered_set<RandomizerCheck>& checks) {
    std::vector<std::string> sohIds;
    sohIds.reserve(checks.size());
    for (RandomizerCheck rc : checks) {
        const std::string_view id = Rando::GetRandomizerCheckTrackerId(rc);
        if (!id.empty()) {
            sohIds.emplace_back(id);
        }
    }
    std::sort(sohIds.begin(), sohIds.end());
    sohIds.erase(std::unique(sohIds.begin(), sohIds.end()), sohIds.end());
    return sohIds;
}

void WriteSohIdList(std::ostream& out, const std::vector<std::string>& sohIds) {
    for (const std::string& sohId : sohIds) {
        out << sohId << '\n';
    }
}

} // namespace

CheckVisibilityDiagnostic BuildCheckVisibilityDiagnostic(RandomizerCheck rc) {
    CheckVisibilityDiagnostic diag;
    diag.check = rc;

    Rando::Location* loc = Rando::StaticData::GetLocation(rc);
    if (loc == nullptr) {
        diag.primaryBlockReason = "Invalid RandomizerCheck (no static location)";
        return diag;
    }

    Rando::ItemLocation* itemLoc = OTRGlobals::Instance->gRandoContext->GetItemLocation(rc);
    diag.mapTrackerId = std::string(Rando::GetRandomizerCheckTrackerId(rc));
    diag.displayName = loc->GetShortName();
    diag.rcType = RCTypeLabel(loc->GetRCType());
    diag.quest = QuestLabel(loc->GetQuest());
    diag.vanillaItem = ItemName(loc->GetVanillaItem());
    diag.placedItem = ItemName(itemLoc->GetPlacedRandomizerGet());

    diag.inChecksByArea = IsInChecksByArea(rc, loc->GetArea());
    diag.isExcluded = itemLoc->IsExcluded();
    diag.questActive = OTRGlobals::Instance->gRandoContext->IsQuestOfLocationActive(rc);
    diag.showKeysanityFlag = showKeysanity;
    diag.smallKeyTypeGatePass = loc->GetRCType() != RCTYPE_SMALL_KEY || showKeysanity ||
                                itemLoc->GetPlacedRandomizerGet() != loc->GetVanillaItem();
    diag.checkShuffled = IsCheckShuffled(rc);
    diag.visibleInTracker = IsVisibleInCheckTracker(rc);
    diag.areaSpoiled = IsAreaSpoiled(loc->GetArea());

    if (mapTrackerState.loaded) {
        diag.mapPackLinked = mapTrackerState.linkedChecks.contains(rc);
        diag.mapPackUnassigned =
            std::find(mapTrackerState.unassignedCheckIds.begin(), mapTrackerState.unassignedCheckIds.end(), rc) !=
            mapTrackerState.unassignedCheckIds.end();
    }

    if (diag.isExcluded) {
        diag.primaryBlockReason = "Location is excluded";
    } else if (!diag.questActive) {
        diag.primaryBlockReason = "Quest mismatch (vanilla location on MQ dungeon or reverse)";
    } else if (loc->GetRCType() == RCTYPE_SMALL_KEY && !showKeysanity &&
               itemLoc->GetPlacedRandomizerGet() == loc->GetVanillaItem()) {
        diag.primaryBlockReason =
            "RCTYPE_SMALL_KEY hidden while Keysanity is Vanilla and placed item is still the vanilla small key";
    } else if (loc->GetRCType() == RCTYPE_BOSS_KEY && !showBossKeysanity) {
        diag.primaryBlockReason = "RCTYPE_BOSS_KEY hidden while Boss Keysanity is Vanilla";
    } else if (!diag.checkShuffled) {
        diag.primaryBlockReason = "IsCheckShuffled returned false (another shuffle filter — pots, scrubs, etc.)";
    } else if (!diag.visibleInTracker) {
        diag.primaryBlockReason = "IsCheckShuffled passed but IsVisibleInCheckTracker is false (unexpected)";
    } else if (IsMapModeEnabled() && mapTrackerState.loaded && !diag.mapPackLinked && !diag.mapPackUnassigned) {
        diag.primaryBlockReason = "Visible in list logic but no map pack marker (pack soh_id / map_locations issue)";
    } else if (IsMapModeEnabled() && mapTrackerState.loaded && diag.mapPackUnassigned) {
        diag.primaryBlockReason = "Visible in game but unassigned in map pack (no matching soh_id placement)";
    } else if (IsMapModeEnabled() && !diag.areaSpoiled && CVarGetInteger(CVAR_TRACKER_CHECK("MQSpoilers"), 0) == 0) {
        diag.primaryBlockReason = "Area not spoiled — need dungeon map or MQ Spoilers for map markers";
    } else {
        diag.primaryBlockReason = "Should be visible";
    }

    return diag;
}

std::string WriteCheckTrackerVisibilityDebugLog() {
    const std::filesystem::path logDir = Ship::Context::GetPathRelativeToAppDirectory("logs");
    const std::filesystem::path logPath = logDir / "check_tracker_visibility_debug.txt";

    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

#ifdef IS_ARCHIPELAGO
    if (IS_ARCHIPELAGO) {
        CheckTracker::RefreshArchipelagoScoutedChecks();
    }
#endif

    std::ofstream out(logPath.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        SPDLOG_ERROR("[CheckTrackerVisibility] Failed to open {}", logPath.generic_string());
        return "";
    }

    std::unordered_set<RandomizerCheck> mapPackLinkedChecks;
    std::unordered_set<RandomizerCheck> trackerVisibleChecks;
    std::unordered_set<RandomizerCheck> mapTrackerRenderableChecks;

    if (mapTrackerState.loaded) {
        mapPackLinkedChecks = mapTrackerState.linkedChecks;
    }

    const bool mqSpoilers = CVarGetInteger(CVAR_TRACKER_CHECK("MQSpoilers"), 0) != 0;
    for (auto& areaEntry : checksByArea) {
        for (RandomizerCheck rc : areaEntry.second) {
            if (!IsVisibleInCheckTracker(rc)) {
                continue;
            }
            trackerVisibleChecks.insert(rc);

            if (!mapTrackerState.loaded || !mapTrackerState.linkedChecks.contains(rc)) {
                continue;
            }

            Rando::Location* loc = Rando::StaticData::GetLocation(rc);
            if (loc != nullptr && (IsAreaSpoiled(loc->GetArea()) || mqSpoilers)) {
                mapTrackerRenderableChecks.insert(rc);
            }
        }
    }

    const std::vector<std::string> mapPackLinkedIds = CollectSortedSohIds(mapPackLinkedChecks);
    const std::vector<std::string> trackerVisibleIds = CollectSortedSohIds(trackerVisibleChecks);
    const std::vector<std::string> mapTrackerRenderableIds = CollectSortedSohIds(mapTrackerRenderableChecks);

    out << "# Ship of Harkinian — check tracker export (soh_id per line below each section header)\n";
    out << "# Use [tracker_visible_locations] to compare against Archipelago location lists.\n";
    out << "# Use [map_tracker_pack_linked] for checks that have a map pack placement.\n\n";

    out << "[summary]\n";
    out << "map_pack_loaded=" << (mapTrackerState.loaded ? "yes" : "no") << '\n';
    if (mapTrackerState.loaded) {
        out << "map_pack=" << mapTrackerState.assetsRoot.string() << '\n';
    }
    out << "map_tracker_pack_linked_count=" << mapPackLinkedIds.size() << '\n';
    out << "map_tracker_renderable_count=" << mapTrackerRenderableIds.size() << '\n';
    out << "tracker_visible_locations_count=" << trackerVisibleIds.size() << '\n';
    out << "tracker_unassigned_on_map_count=" << mapTrackerState.unassignedCheckIds.size() << '\n';
    out << "IS_RANDO=" << (IS_RANDO ? "yes" : "no") << '\n';
#ifdef IS_ARCHIPELAGO
    out << "IS_ARCHIPELAGO=" << (IS_ARCHIPELAGO ? "yes" : "no") << '\n';
#endif
    out << "RSK_KEYSANITY=" << KeysanitySettingLabel() << '\n';
#ifdef IS_ARCHIPELAGO
    if (IS_ARCHIPELAGO) {
        ArchipelagoClient& apClient = ArchipelagoClient::GetInstance();
        const std::vector<ArchipelagoClient::ApItem>& scoutedItems = apClient.GetScoutedItems();
        const std::unordered_set<RandomizerCheck>& scoutedCache = CheckTracker::GetArchipelagoScoutedChecks();
        const std::vector<std::string>& unresolvedNames = CheckTracker::GetArchipelagoScoutedUnresolvedLocationNames();
        const int connectionStatus = CVarGetInteger(CVAR_REMOTE_ARCHIPELAGO("ConnectionStatus"), 0);

        out << "archipelago_connected=" << (apClient.IsConnected() ? "yes" : "no") << '\n';
        out << "archipelago_connection_status=" << connectionStatus << '\n';
        out << "archipelago_scouted_count=" << scoutedItems.size() << '\n';
        out << "archipelago_scouted_mapped_cache_count=" << scoutedCache.size() << '\n';
        out << "archipelago_scouted_unresolved_name_count=" << unresolvedNames.size() << '\n';

        if (scoutedItems.empty() && scoutedCache.empty() && connectionStatus == 4) {
            out << "archipelago_export_warning=ConnectionStatus is 4 (scouted) but scout lists are empty; "
                   "tracker may be using non-AP visibility fallback\n";
        } else if (scoutedItems.empty() && !scoutedCache.empty()) {
            out << "archipelago_export_warning=AP client scout vector empty but tracker cache has entries\n";
        } else if (!scoutedItems.empty() && scoutedCache.empty()) {
            out << "archipelago_export_warning=AP scout vector populated but tracker cache empty (refresh failed?)\n";
        }
    }
#endif
    out << '\n';

    out << "[map_tracker_pack_linked]\n";
    WriteSohIdList(out, mapPackLinkedIds);
    out << '\n';

    out << "[map_tracker_renderable]\n";
    out << "# Linked in pack and visible in tracker (markers show when area is spoiled or MQ Spoilers is on)\n";
    WriteSohIdList(out, mapTrackerRenderableIds);
    out << '\n';

    out << "[tracker_visible_locations]\n";
    out << "# All locations shown in the in-game check tracker (compare this count to Archipelago)\n";
    WriteSohIdList(out, trackerVisibleIds);
    out << '\n';

    if (!mapTrackerState.unassignedCheckIds.empty()) {
        std::unordered_set<RandomizerCheck> unassignedSet(mapTrackerState.unassignedCheckIds.begin(),
                                                          mapTrackerState.unassignedCheckIds.end());
        out << "[tracker_visible_but_missing_from_map_pack]\n";
        WriteSohIdList(out, CollectSortedSohIds(unassignedSet));
        out << '\n';
    }

#ifdef IS_ARCHIPELAGO
    if (IS_ARCHIPELAGO) {
        const std::vector<ArchipelagoClient::ApItem>& scoutedItems = ArchipelagoClient::GetInstance().GetScoutedItems();
        std::vector<std::string> scoutedApNames;
        std::vector<std::string> scoutedNotVisibleLines;
        size_t scoutedVisibleCount = 0;

        scoutedApNames.reserve(scoutedItems.size());
        scoutedNotVisibleLines.reserve(32);

        for (const ArchipelagoClient::ApItem& apItem : scoutedItems) {
            scoutedApNames.push_back(apItem.locationName);

            const std::optional<RandomizerCheck> rcOpt = Rando::StaticData::TryResolveLocationName(apItem.locationName);
            if (!rcOpt.has_value()) {
                scoutedNotVisibleLines.push_back(fmt::format("\tunknown_rc\t{}\t(not in locationNameToEnum or AP alias)",
                                                             apItem.locationName));
                continue;
            }

            const RandomizerCheck rc = *rcOpt;
            const std::string_view sohId = Rando::GetRandomizerCheckTrackerId(rc);
            const bool visible = IsVisibleInCheckTracker(rc);
            if (visible) {
                scoutedVisibleCount++;
            } else {
                const CheckVisibilityDiagnostic diag = BuildCheckVisibilityDiagnostic(rc);
                scoutedNotVisibleLines.push_back(
                    fmt::format("{}\t{}\t{}", sohId.empty() ? "?" : std::string(sohId), apItem.locationName,
                                diag.primaryBlockReason));
            }
        }

        std::sort(scoutedApNames.begin(), scoutedApNames.end());
        std::sort(scoutedNotVisibleLines.begin(), scoutedNotVisibleLines.end());

        out << "[archipelago_scouted_summary]\n";
        out << "archipelago_scouted_visible_in_tracker_count=" << scoutedVisibleCount << '\n';
        out << "archipelago_scouted_hidden_from_tracker_count=" << scoutedNotVisibleLines.size() << '\n';
        out << '\n';

        out << "[archipelago_scouted_ap_names]\n";
        out << "# One AP location name per line (same strings as the Archipelago website / ChecksArchipelago.txt)\n";
        for (const std::string& name : scoutedApNames) {
            out << name << '\n';
        }
        out << '\n';

        out << "[archipelago_scouted_not_in_tracker]\n";
        out << "# soh_id<TAB>ap_location_name<TAB>block_reason — diff this section against your AP export\n";
        for (const std::string& line : scoutedNotVisibleLines) {
            out << line << '\n';
        }
        out << '\n';
    }
#endif

    out.flush();
    SPDLOG_INFO("[CheckTrackerVisibility] Wrote {} (visible={}, map_linked={})", logPath.string(),
                trackerVisibleIds.size(), mapPackLinkedIds.size());
    return logPath.string();
}

void DrawCheckTrackerVisibilityDebugControls() {
    if (ImGui::Button("Export tracker check list")) {
        const std::string path = WriteCheckTrackerVisibilityDebugLog();
        if (!path.empty()) {
            CVarSetString(CHECK_TRACKER_VISIBILITY_DEBUG_CVAR, path.c_str());
            CVarSave();
            SPDLOG_INFO("[CheckTrackerVisibility] Log written to {}", path);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Writes logs/check_tracker_visibility_debug.txt with soh_id lists.\n"
            "Section [tracker_visible_locations] matches the in-game tracker count.\n"
            "Section [archipelago_scouted_not_in_tracker] lists AP locations hidden by tracker filters.\n"
            "Section [map_tracker_pack_linked] lists checks placed on the map pack.");
    }

    const char* lastPath = CVarGetString(CHECK_TRACKER_VISIBILITY_DEBUG_CVAR, "");
    if (lastPath != nullptr && lastPath[0] != '\0') {
        ImGui::TextDisabled("Last export: %s", lastPath);
    }

    {
        size_t visibleCount = 0;
        for (auto& areaEntry : checksByArea) {
            for (RandomizerCheck rc : areaEntry.second) {
                if (IsVisibleInCheckTracker(rc)) {
                    visibleCount++;
                }
            }
        }
        ImGui::TextDisabled("Tracker visible: %zu | Map pack linked: %zu", visibleCount,
                            mapTrackerState.loaded ? mapTrackerState.linkedChecks.size() : 0);
    }
}

} // namespace CheckTracker
