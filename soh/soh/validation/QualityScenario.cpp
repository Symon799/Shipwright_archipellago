#include "QualityScenario.h"
#include <cstring>
#include <cstdio>

#ifdef SOH_ENABLE_QUALITY_SCENARIOS
#include "soh/OTRGlobals.h"
#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer_map_tracker.h"
#include "soh/Enhancements/randomizer/map_tracker_internal.h"
#include "soh/Enhancements/randomizer/map_tracker_pause.h"
#include "soh/Network/Archipelago/ArchipelagoSession.h"
#include "soh/Network/Archipelago/Archipelago.h"
#include <ship/config/Config.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

extern "C" {
#include "global.h"
extern PlayState* gPlayState;
extern GameState* gGameState;
int MapTracker_IsGameplayPaused(void);
}

namespace {
using json = nlohmann::json;
bool active = false;
bool passed = false;
bool finished = false;
std::string scenario;
int phase = 0;
int phaseFrames = 0;
bool actionSent = false;
int previousPage = 0;
int frame = 0;
int markerFrame = -1;
int popupFrame = -1;
ImVec2 markerMin, markerMax, popupMin, popupMax;
Vec3f frozenPosition{};
std::chrono::steady_clock::time_point started;
std::chrono::steady_clock::time_point phaseStarted;
json result = { { "status", "NOT_RUN" }, { "checks", json::array() } };

void Check(bool condition, const char* name) {
    result["checks"].push_back({ { "name", name }, { "passed", condition } });
    if (!condition)
        throw std::runtime_error(name);
}
void Advance() {
    ++phase;
    phaseFrames = 0;
    actionSent = false;
    phaseStarted = std::chrono::steady_clock::now();
    result["phase"] = phase;
    std::ofstream("validation-progress.json") << result.dump(2);
}
void Close(bool success, const std::string& reason = "") {
    finished = true;
    passed = success;
    result["status"] = success ? "PASSED" : "FAILED";
    result["reason"] = reason;
    Ship::Context::GetInstance()->GetWindow()->Close();
}

void CreateDisposableSave() {
    SaveManager::Instance->Init();
    SaveManager::Instance->InitFile(false);
    gSaveContext.fileNum = 0;
    gSaveContext.rupees = 37;
    gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex = 2;
    SaveManager::Instance->SaveFile(0);
    SaveManager::Instance->ThreadPoolWait();
    gSaveContext.rupees = 0;
    gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex = 0;
    SaveManager::Instance->LoadFile(0);
    Check(gSaveContext.rupees == 37, "SaveManager threaded write/load roundtrip");
    Check(gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex == 2,
          "Saved AP received-item index roundtrip");
    Archipelago::Item noItem;
    noItem.itemName = "No Item";
    ArchipelagoClient::GetInstance().QueueItem(noItem);
    Check(gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex == 3, "Known No Item acknowledges one event");
    gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex = 2;
    auto config = Ship::Context::GetInstance()->GetConfig();
    const auto key = Archipelago::ConnectionProfileKey("validation-seed-a", "Validation");
    config->SetString(key + ".ServerAddress", "127.0.0.1:38202");
    config->SetString(key + ".Password", "");
    config->Save();
    config->Reload();
    Check(config->GetString(key + ".ServerAddress", "") == "127.0.0.1:38202", "Config connection-profile persistence");
    // Use the same save loading boundary/hooks as FileChoose_LoadGame, not a tracker-only bootstrap.
    Sram_OpenSave();
    gSaveContext.entranceIndex = ENTR_LINKS_HOUSE_CHILD_SPAWN;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    gSaveContext.respawnFlag = 0;
    SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    gGameState->running = false;
    GameInteractor_ExecuteOnLoadGame(gSaveContext.fileNum);
    GameInteractor_ExecutePostLoadGame(gSaveContext.fileNum);
}

bool MapOpen() {
    return scenario.ends_with("3d") ? MapTracker::NativePauseInteractive() : MapTracker_IsGameplayPaused() != 0;
}

void BeforeFrame() {
    if (!active || finished || !gPlayState)
        return;
    auto& input = gPlayState->state.input[0];
    if (phase == 1 && KaleidoSetup_CanPause(gPlayState)) {
        input.press.button |= BTN_START;
        input.cur.button |= BTN_START;
        Advance();
    } else if (phase == 3 && phaseFrames == 1) {
        input.press.button |= BTN_R;
        input.cur.button |= BTN_R;
    } else if (phase == 4 && phaseFrames == 1) {
        input.press.button |= BTN_START;
        input.cur.button |= BTN_START;
    } else if ((phase == 6 || phase == 10) && !actionSent &&
               std::chrono::steady_clock::now() - phaseStarted > std::chrono::milliseconds(750)) {
        MapTracker::ToggleInGameMap(true);
        actionSent = true;
    } else if (phase == 9 && phaseFrames == 1) {
        MapTracker::ToggleInGameMap(true);
    } else if (phase == 7 || phase == 8 || phase == 11) {
        MapTracker::screenCursorUsesController = true;
        MapTracker::screenCursorNeedsCenter = false;
        if (phase == 7 && markerFrame >= 0)
            MapTracker::screenCursorPosition = ImVec2((markerMin.x + markerMax.x) / 2, (markerMin.y + markerMax.y) / 2);
        if ((phase == 8 || phase == 11) && popupFrame >= 0)
            MapTracker::screenCursorPosition = ImVec2((popupMin.x + popupMax.x) / 2, (popupMin.y + popupMax.y) / 2);
        // Real gameplay input is injected after polling; the production tracker must suppress it.
        input.cur.stick_x = 60;
        input.cur.button |= BTN_A;
        input.press.button |= BTN_A;
    }
}

void AfterFrame() {
    if (!active || finished)
        return;
    ++frame;
    ++phaseFrames;
    result["phase"] = phase;
    result["frame"] = frame;
    if (gPlayState) {
        result["pause_state"] = gPlayState->pauseCtx.state;
        result["map_open"] = MapOpen();
        result["marker_frame"] = markerFrame;
        result["popup_frame"] = popupFrame;
    }
    try {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60))
            throw std::runtime_error("Scenario timeout in phase " + std::to_string(phase));
        if (phase == 0 && gGameState && !gPlayState) {
            if (scenario == "pack-invalid") {
                CheckTracker::LoadMapTrackerData();
                Check(!CheckTracker::mapTrackerState.loaded && !CheckTracker::mapTrackerState.fatalErrors.empty(),
                      "Actual pack metadata parser reports malformed root without crashing");
                Close(true);
                return;
            }
            if (scenario == "persist") {
                SaveManager::Instance->Init();
                SaveManager::Instance->LoadFile(0);
                Check(gSaveContext.rupees == 37, "Save survives a fresh game process");
                Check(gSaveContext.ship.quest.data.archipelago.lastReceivedItemIndex == 2, "AP index survives restart");
                const auto key = Archipelago::ConnectionProfileKey("validation-seed-a", "Validation");
                Check(Ship::Context::GetInstance()->GetConfig()->GetString(key + ".ServerAddress", "") ==
                          "127.0.0.1:38202",
                      "Connection profile survives restart");
                Close(true);
                return;
            }
            CreateDisposableSave();
            Advance();
        } else if (gPlayState) {
            if (phase == 6 && MapOpen() && CheckTracker::mapTrackerState.loaded &&
                scenario.starts_with("exit-loading")) {
                CheckTracker::ResetMapTrackerState(true);
                CheckTracker::LoadMapTrackerData();
                CheckTracker::ProcessPendingMapTextureLoads();
                Check(CheckTracker::HasPendingMapTextureWork(), "Exit with an outstanding real texture request/result");
                Close(true);
                return;
            }
            if (phase == 2 && gPlayState->pauseCtx.state == 6) {
                previousPage = gPlayState->pauseCtx.pageIndex;
                Check(true, "Vanilla Start opens the real pause menu");
                Advance();
            } else if (phase == 3 && phaseFrames > 20 && gPlayState->pauseCtx.unk_1E4 == 0 &&
                       gPlayState->pauseCtx.pageIndex != previousPage) {
                previousPage = gPlayState->pauseCtx.pageIndex;
                Check(true, "Vanilla shoulder navigation remains functional");
                Advance();
            } else if (phase == 4 && gPlayState->pauseCtx.state == 0) {
                Check(true, "Vanilla Start closes pause");
                Advance();
            } else if (phase == 5 && phaseFrames > 20 && KaleidoSetup_CanPause(gPlayState)) {
                Advance();
            } else if (phase == 6 && MapOpen() && CheckTracker::mapTrackerState.loaded &&
                       !CheckTracker::mapTrackerState.tabs.empty() &&
                       CheckTracker::mapTrackerState.tabs[CheckTracker::mapTrackerState.selectedTabIndex].imageLoaded) {
                Check(true, "Production toggle opens the requested map mode");
                Check(CheckTracker::mapTrackerState.loaded, "Real archive and metadata parser load the synthetic pack");
                frozenPosition = GET_PLAYER(gPlayState)->actor.world.pos;
                Advance();
            } else if (phase == 7 && phaseFrames > 20 && popupFrame >= frame - 2) {
                Check(true, "Rendered multicheck popup opens at the rendered marker");
                Advance();
            } else if (phase == 8 && phaseFrames > 20) {
                Check(popupFrame >= frame - 2, "Popup remains open when cursor enters its rendered list");
                const Vec3f position = GET_PLAYER(gPlayState)->actor.world.pos;
                Check(std::abs(position.x - frozenPosition.x) < 0.01f &&
                          std::abs(position.z - frozenPosition.z) < 0.01f,
                      "Gameplay movement is blocked beneath the map");
                Advance();
            } else if (phase == 9 && phaseFrames > 20 && !MapOpen() && gPlayState->pauseCtx.state == 0) {
                Check(true, "Map close releases pause without a stuck state");
                Advance();
            } else if (phase == 10 && MapOpen()) {
                MapTracker::screenCursorUsesController = true;
                MapTracker::screenCursorNeedsCenter = false;
                MapTracker::screenCursorPosition =
                    ImVec2((markerMin.x + markerMax.x) / 2, (markerMin.y + markerMax.y) / 2);
                Advance();
            } else if (phase == 11 && phaseFrames > 20 && popupFrame >= frame - 2) {
                SaveManager::Instance->SaveFile(0);
                Check(true, "Exit requested with rendered map/popup and a threaded save");
                Close(true);
            }
        }
    } catch (const std::exception& error) { Close(false, error.what()); }
}
} // namespace

namespace SohValidation {
void ObserveMultiMarker(ImVec2 min, ImVec2 max) {
    if (active) {
        markerMin = min;
        markerMax = max;
        markerFrame = frame;
    }
}
void ObservePopup(ImVec2 min, ImVec2 max) {
    if (active) {
        popupMin = min;
        popupMax = max;
        popupFrame = frame;
    }
}
} // namespace SohValidation

extern "C" int SohValidation_Configure(int argc, char** argv) {
    bool requested = false;
    for (int i = 1; i < argc; ++i)
        requested |= std::strcmp(argv[i], "--quality-validation") == 0;
    if (!requested)
        return 0;
    try {
        const auto cwd = std::filesystem::canonical(std::filesystem::current_path());
        if (std::filesystem::weakly_canonical(Ship::Context::GetAppDirectoryPath("soh")) != cwd)
            throw std::runtime_error("Non-portable configuration would escape the isolated runtime");
        if (std::filesystem::canonical(std::filesystem::path(argv[0])).parent_path() != cwd)
            throw std::runtime_error("Validation executable must be staged inside its isolated runtime");
        std::ifstream input("validation-runtime.json");
        json manifest;
        input >> manifest;
        if (manifest.at("version") != 1 || manifest.at("disposable") != true)
            throw std::runtime_error("An explicit disposable runtime manifest is required");
        scenario = manifest.at("scenario").get<std::string>();
        if (scenario != "pause-3d" && scenario != "pause-2d" && scenario != "persist" && scenario != "pack-invalid" &&
            scenario != "exit-loading-3d" && scenario != "exit-loading-2d")
            throw std::runtime_error("Unknown quality scenario");
        active = true;
        started = std::chrono::steady_clock::now();
        result["scenario"] = scenario;
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Validation refused: %s\n", error.what());
        return -1;
    }
}
extern "C" void SohValidation_Register() {
    if (!active)
        return;
    if (std::filesystem::weakly_canonical(Ship::Context::GetAppDirectoryPath()) != std::filesystem::current_path()) {
        Close(false, "Non-portable configuration would escape the isolated runtime");
        return;
    }
    CVarSetInteger(MapTracker::CVAR_DISPLAY_MODE, scenario.ends_with("2d") ? 1 : 0);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>(BeforeFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(AfterFrame);
}
extern "C" int SohValidation_Finish() {
    if (!active)
        return 0;
    if (!finished) {
        result["status"] = "FAILED";
        result["reason"] = "Game exited before the scenario completed";
    }
    result["shutdown_complete"] = true;
    std::ofstream("validation-result.json") << result.dump(2);
    return finished && passed ? 0 : 1;
}
#else
namespace SohValidation {
void ObserveMultiMarker(ImVec2, ImVec2) {
}
void ObservePopup(ImVec2, ImVec2) {
}
} // namespace SohValidation
extern "C" int SohValidation_Configure(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quality-validation") == 0) {
            std::fprintf(stderr, "Quality scenarios require a Debug build with SOH_ENABLE_QUALITY_SCENARIOS=ON\n");
            return -1;
        }
    }
    return 0;
}
extern "C" void SohValidation_Register() {
}
extern "C" int SohValidation_Finish() {
    return 0;
}
#endif
