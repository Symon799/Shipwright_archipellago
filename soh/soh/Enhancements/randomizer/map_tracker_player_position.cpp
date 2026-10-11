#include "map_tracker_internal.h"
#include "map_tracker_link_resolver.h"
#include "map_tracker_projection.h"
#include "randomizer_map_tracker.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/ResourceManagerHelpers.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetAlternateHeaders.h"
#include "soh/resource/type/scenecommand/SetExitList.h"

#include <algorithm>
#include <cmath>
#include <map>

extern "C" {
#include "global.h"
extern PlayState* gPlayState;
}

namespace CheckTracker {
namespace {

std::string_view GetSceneMapId(int scene) {
    using namespace MapIds;
    switch (scene) {
        case SCENE_HYRULE_FIELD: return HyruleFields;
        case SCENE_KOKIRI_FOREST: return KokiriForest;
        case SCENE_LOST_WOODS: return LostWoods;
        case SCENE_SACRED_FOREST_MEADOW: return SacredForestMeadow;
        case SCENE_KAKARIKO_VILLAGE: return KakarikoVillage;
        case SCENE_GRAVEYARD: return Graveyard;
        case SCENE_DEATH_MOUNTAIN_TRAIL: return Dmt;
        case SCENE_DEATH_MOUNTAIN_CRATER: return Dmc;
        case SCENE_GORON_CITY: return GoronCity;
        case SCENE_ZORAS_RIVER: return ZoraRiver;
        case SCENE_ZORAS_DOMAIN: return ZorasDomain;
        case SCENE_ZORAS_FOUNTAIN: return ZorasFountain;
        case SCENE_LAKE_HYLIA: return LakeHylia;
        case SCENE_LON_LON_RANCH: return LonLonRanch;
        case SCENE_GERUDO_VALLEY: return GerudoValley;
        case SCENE_GERUDOS_FORTRESS: return GerudoFortress;
        case SCENE_HAUNTED_WASTELAND: return Wasteland;
        case SCENE_DESERT_COLOSSUS: return DesertColossus;
        case SCENE_HYRULE_CASTLE: return HyruleCastle;
        case SCENE_OUTSIDE_GANONS_CASTLE: return GanonsCastle;
        case SCENE_MARKET_ENTRANCE_DAY:
        case SCENE_MARKET_ENTRANCE_NIGHT:
        case SCENE_MARKET_ENTRANCE_RUINS:
        case SCENE_MARKET_DAY:
        case SCENE_MARKET_NIGHT:
        case SCENE_MARKET_RUINS:
        case SCENE_BACK_ALLEY_DAY:
        case SCENE_BACK_ALLEY_NIGHT:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS: return Market;
        case SCENE_TEMPLE_OF_TIME: return TempleOfTime;
        case SCENE_DEKU_TREE: return DekuTree;
        case SCENE_DODONGOS_CAVERN: return DodongosCavern;
        case SCENE_JABU_JABU: return JabuJabusBelly;
        case SCENE_FOREST_TEMPLE: return ForestTemple;
        case SCENE_FIRE_TEMPLE: return FireTemple;
        case SCENE_WATER_TEMPLE: return WaterTemple;
        case SCENE_SPIRIT_TEMPLE: return SpiritTemple;
        case SCENE_SHADOW_TEMPLE: return ShadowTemple;
        case SCENE_BOTTOM_OF_THE_WELL: return BottomOfTheWell;
        case SCENE_ICE_CAVERN: return IceCavern;
        case SCENE_GERUDO_TRAINING_GROUND: return GerudoTrainingGround;
        case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_GANONS_TOWER: return GanonsTower;
        default: return {};
    }
}

size_t FindActiveExitCount(SOH::Scene* scene, const s16* exits, int depth = 0) {
    if (scene == nullptr || depth > 8) {
        return 0;
    }
    for (const auto& command : scene->commands) {
        if (auto list = std::dynamic_pointer_cast<SOH::SetExitList>(command);
            list != nullptr && static_cast<void*>(list->exits.data()) == static_cast<const void*>(exits)) {
            return list->exits.size();
        }
        if (auto alternatives = std::dynamic_pointer_cast<SOH::SetAlternateHeaders>(command)) {
            for (const auto& header : alternatives->headers) {
                const size_t count = FindActiveExitCount(header.get(), exits, depth + 1);
                if (count != 0) {
                    return count;
                }
            }
        }
    }
    return 0;
}

std::string_view GetExitMapId(std::string_view sourceMap, int entranceIndex) {
    const auto& entrance = gEntranceTable[entranceIndex];
    // The bridge shares the Lost Woods scene, but the pack folds it into the Kokiri/Field connection.
    if (entrance.scene == SCENE_LOST_WOODS && (entrance.spawn == 8 || entrance.spawn == 9)) {
        if (sourceMap == MapIds::KokiriForest) {
            return MapIds::HyruleFields;
        }
        if (sourceMap == MapIds::HyruleFields) {
            return MapIds::KokiriForest;
        }
    }
    return GetSceneMapId(entrance.scene);
}

std::optional<MapTracker::PositionProjection> GetSceneMinimapProjection() {
    // Use the canonical minimap coordinates, independent of HUD margins, visibility and cosmetic mirroring.
    if (gMapData == nullptr || gPlayState->sceneNum < SCENE_HYRULE_FIELD ||
        gPlayState->sceneNum > SCENE_OUTSIDE_GANONS_CASTLE) {
        return std::nullopt;
    }
    const int index = gPlayState->sceneNum - SCENE_HYRULE_FIELD;
    const auto& compass = gMapData->owCompassInfo[index];
    return MapTracker::MakeMinimapProjection(compass[0], compass[1],
        SCREEN_WIDTH / 2.0 + compass[2] / 10.0 - gMapData->owMinimapPosX[index],
        SCREEN_HEIGHT / 2.0 - compass[3] / 10.0 - gMapData->owMinimapPosY[index]);
}

struct ProjectionCache {
    bool ready = false;
    void* scene = nullptr;
    CollisionHeader* collision = nullptr;
    const s16* exits = nullptr;
    std::string mapId;
    uint64_t packGeneration = 0;
    std::optional<MapTracker::PositionProjection> minimap;
    std::optional<MapTracker::PositionProjection> projection;
};

ProjectionCache projectionCache;

void BuildProjection(const MapTabData& tab) {
    projectionCache = { true, gPlayState->sceneSegment, gPlayState->colCtx.colHeader,
                        gPlayState->setupExitList, tab.mapId, mapTrackerState.textureLoadGeneration };
    projectionCache.minimap = GetSceneMinimapProjection();
    if (!projectionCache.minimap) {
        return;
    }
    const size_t exitCount = FindActiveExitCount(static_cast<SOH::Scene*>(gPlayState->sceneSegment),
                                               gPlayState->setupExitList);
    const CollisionHeader* collision = gPlayState->colCtx.colHeader;
    if (exitCount == 0 || collision == nullptr || collision->polyList == nullptr || collision->vtxList == nullptr ||
        collision->surfaceTypeList == nullptr) {
        return;
    }

    struct ExitCenter { double x = 0, z = 0, weight = 0; };
    std::map<size_t, ExitCenter> centers;
    // Exit surfaces are the physical source-side anchors, independent of HUD placement or minimap visibility.
    for (size_t i = 0; i < collision->numPolygons; ++i) {
        CollisionPoly* poly = &collision->polyList[i];
        const size_t exitIndex = SurfaceType_GetSceneExitIndex(&gPlayState->colCtx, poly, BGCHECK_SCENE);
        if (exitIndex == 0 || exitIndex > exitCount) {
            continue;
        }
        const auto a = COLPOLY_VTX_INDEX(poly->vtxData[0]);
        const auto b = COLPOLY_VTX_INDEX(poly->vtxData[1]);
        const auto c = COLPOLY_VTX_INDEX(poly->vtxData[2]);
        if (a >= collision->numVertices || b >= collision->numVertices || c >= collision->numVertices) {
            continue;
        }
        const Vec3s& v0 = collision->vtxList[a];
        const Vec3s& v1 = collision->vtxList[b];
        const Vec3s& v2 = collision->vtxList[c];
        const double ux = v1.x - v0.x, uy = v1.y - v0.y, uz = v1.z - v0.z;
        const double vx = v2.x - v0.x, vy = v2.y - v0.y, vz = v2.z - v0.z;
        const double area = std::sqrt(std::pow(uy * vz - uz * vy, 2) + std::pow(uz * vx - ux * vz, 2) +
                                      std::pow(ux * vy - uy * vx, 2));
        auto& center = centers[exitIndex - 1];
        center.x += (v0.x + v1.x + v2.x) / 3.0 * area;
        center.z += (v0.z + v1.z + v2.z) / 3.0 * area;
        center.weight += area;
    }

    std::map<std::string_view, std::vector<MapTracker::PositionAnchor>> candidates;
    for (const auto& [exitIndex, center] : centers) {
        const int entranceIndex = gPlayState->setupExitList[exitIndex];
        if (center.weight <= 0 || entranceIndex < 0 || entranceIndex >= ENTR_MAX) {
            continue;
        }
        const auto destination = GetExitMapId(tab.mapId, entranceIndex);
        if (!destination.empty() && destination != tab.mapId) {
            auto anchor = projectionCache.minimap->Project(center.x / center.weight, center.z / center.weight);
            candidates[destination].push_back({ anchor.mapX, anchor.mapY, 0, 0 });
        }
    }
    std::vector<MapTracker::PositionAnchor> anchors;
    for (const auto& link : tab.links) {
        const auto exits = candidates.find(link.targetMapId);
        // The existing pack does not identify individual duplicate links. Do not guess their correspondence.
        if (exits == candidates.end() || exits->second.size() != 1 ||
            std::count_if(tab.links.begin(), tab.links.end(), [&](const MapLink& other) {
                return other.targetMapId == link.targetMapId;
            }) != 1) {
            continue;
        }
        auto anchor = exits->second.front();
        anchor.mapX = link.x;
        anchor.mapY = link.y;
        anchors.push_back(anchor);
    }
    projectionCache.projection = MapTracker::FitPositionProjection(anchors);
}

static RegisterShipInitFunc positionHooks([] {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) { projectionCache = {}; });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) { projectionCache = {}; });
});

} // namespace

std::optional<MapPlayerIndicator> GetExperimentalMapPlayerIndicator(const MapTabData& tab) {
    if (CVarGetInteger(MapTracker::CVAR_PLAYER_POSITION, 0) == 0 || !GameInteractor::IsSaveLoaded() ||
        gPlayState == nullptr) {
        return std::nullopt;
    }
    // These tracker layouts cannot be reliably mapped with one shared projection.
    // Check the scene as well as the tab so the isolated Lost Woods bridge is excluded too.
    if (gPlayState->sceneNum == SCENE_LOST_WOODS || gPlayState->sceneNum == SCENE_HAUNTED_WASTELAND ||
        gPlayState->sceneNum == SCENE_LAKE_HYLIA || tab.mapId == MapIds::LostWoods ||
        tab.mapId == MapIds::Wasteland || tab.mapId == MapIds::LakeHylia) {
        return std::nullopt;
    }
    const Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return std::nullopt;
    }
    const double angle = player->actor.shape.rot.y * (3.14159265358979323846 / 32768.0);
    const double worldDx = std::sin(angle), worldDz = std::cos(angle);
    auto makeIndicator = [&](const MapTracker::PositionProjection& projection, double x, double z,
                             double dx, double dz) -> std::optional<MapPlayerIndicator> {
        const auto point = projection.Project(x, z);
        if (!std::isfinite(point.mapX) || !std::isfinite(point.mapY) || point.mapX < 0 || point.mapY < 0 ||
            point.mapX > tab.textureSize.x || point.mapY > tab.textureSize.y) return std::nullopt;
        const double mapDx = projection.xx * dx + projection.xz * dz;
        const double mapDy = projection.zx * dx + projection.zz * dz;
        const double length = std::hypot(mapDx, mapDy);
        if (!std::isfinite(length) || length < 0.000001) return std::nullopt;
        return MapPlayerIndicator{ { static_cast<float>(point.mapX), static_cast<float>(point.mapY) },
                                   { static_cast<float>(mapDx / length), static_cast<float>(mapDy / length) } };
    };
    const MapTracker::CalibrationProfile* profile = nullptr;
    int specificity = -1;
    bool ambiguous = false;
    for (const auto& candidate : tab.playerCalibrations) {
        if (!candidate.Matches(gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num, gSaveContext.sceneSetupIndex,
                               ResourceMgr_IsSceneMasterQuest(gPlayState->sceneNum) != 0)) continue;
        const int score = (candidate.room ? 4 : 0) + (!candidate.setups.empty() ? 2 : 0) + (candidate.layout != "any" ? 1 : 0);
        if (score > specificity) {
            profile = &candidate; specificity = score; ambiguous = false;
        } else if (score == specificity) {
            ambiguous = true;
        }
    }
    if (profile != nullptr && !ambiguous) {
        const auto projection = profile->mesh.ProjectionAt(player->actor.world.pos.x, player->actor.world.pos.z);
        // Respect a calibrated profile's explicit outside-hull policy rather than replacing it with a guess.
        return projection ? makeIndicator(*projection, player->actor.world.pos.x, player->actor.world.pos.z,
                                          worldDx, worldDz) : std::nullopt;
    }
    const auto currentMapId = GetSceneMapId(gPlayState->sceneNum);
    if (currentMapId.empty() || tab.mapId != currentMapId) {
        return std::nullopt;
    }
    if (!projectionCache.ready || projectionCache.scene != gPlayState->sceneSegment ||
        projectionCache.collision != gPlayState->colCtx.colHeader || projectionCache.exits != gPlayState->setupExitList ||
        projectionCache.mapId != tab.mapId || projectionCache.packGeneration != mapTrackerState.textureLoadGeneration) {
        BuildProjection(tab);
    }
    if (!projectionCache.projection) {
        return std::nullopt;
    }
    const auto& projection = *projectionCache.projection;
    const auto minimapPoint = projectionCache.minimap->Project(player->actor.world.pos.x, player->actor.world.pos.z);
    const auto& minimap = *projectionCache.minimap;
    const double minimapDx = minimap.xx * worldDx;
    const double minimapDy = minimap.zz * worldDz;
    return makeIndicator(projection, minimapPoint.mapX, minimapPoint.mapY, minimapDx, minimapDy);
}

} // namespace CheckTracker
