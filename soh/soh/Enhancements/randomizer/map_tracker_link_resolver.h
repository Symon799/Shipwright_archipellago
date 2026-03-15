#pragma once

#include <cstdint>
#include <string_view>

namespace CheckTracker {

namespace MapIds {
inline constexpr std::string_view BottomOfTheWell = "bottom_of_the_well";
inline constexpr std::string_view DekuTree = "deku_tree";
inline constexpr std::string_view DesertColossus = "desert_colossus";
inline constexpr std::string_view DodongosCavern = "dodongos_cavern";
inline constexpr std::string_view Dmc = "dmc";
inline constexpr std::string_view Dmt = "dmt";
inline constexpr std::string_view FireTemple = "fire_temple";
inline constexpr std::string_view ForestTemple = "forest_temple";
inline constexpr std::string_view GanonsCastle = "ganons_castle";
inline constexpr std::string_view GanonsTower = "ganons_tower";
inline constexpr std::string_view GerudoFortress = "gerudo_fortress";
inline constexpr std::string_view GerudoTrainingGround = "gerudo_training_ground";
inline constexpr std::string_view GerudoValley = "gerudo_valley";
inline constexpr std::string_view GoronCity = "goron_city";
inline constexpr std::string_view Graveyard = "graveyard";
inline constexpr std::string_view HyruleCastle = "hyrule_castle";
inline constexpr std::string_view HyruleFields = "hyrule_fields";
inline constexpr std::string_view IceCavern = "ice_cavern";
inline constexpr std::string_view JabuJabusBelly = "jabu_jabus_belly";
inline constexpr std::string_view KakarikoVillage = "kakariko_village";
inline constexpr std::string_view KokiriForest = "kokiri_forest";
inline constexpr std::string_view LakeHylia = "lake_hylia";
inline constexpr std::string_view LonLonRanch = "lon_lon_ranch";
inline constexpr std::string_view LostWoods = "lost_woods";
inline constexpr std::string_view Market = "market";
inline constexpr std::string_view Overworld = "overworld";
inline constexpr std::string_view SacredForestMeadow = "sfm";
inline constexpr std::string_view ShadowTemple = "shadow_temple";
inline constexpr std::string_view SpiritTemple = "spirit_temple";
inline constexpr std::string_view TempleOfTime = "temple_of_time";
inline constexpr std::string_view Wasteland = "wasteland";
inline constexpr std::string_view WaterTemple = "water_temple";
inline constexpr std::string_view ZoraRiver = "zora_river";
inline constexpr std::string_view ZorasDomain = "zoras_domain";
inline constexpr std::string_view ZorasFountain = "zoras_fountain";
} // namespace MapIds

int16_t ResolveMapLinkEntranceIndex(std::string_view sourceMapId, std::string_view targetMapId);

} // namespace CheckTracker
