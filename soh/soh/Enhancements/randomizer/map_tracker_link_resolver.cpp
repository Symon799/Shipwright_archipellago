#include "map_tracker_link_resolver.h"

#include "entrance.h"

#include <array>
#include <tuple>
#include <utility>

namespace CheckTracker {

int16_t ResolveMapLinkEntranceIndex(std::string_view sourceMapId, std::string_view targetMapId) {
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

} // namespace CheckTracker
