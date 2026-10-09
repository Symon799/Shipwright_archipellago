#include "randomizer_map_tracker.h"

#include "map_tracker_internal.h"
#include "randomizer_check_tracker.h"
#include "check_tracker_visibility_debug.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <algorithm>
#include <map>
#include <SDL2/SDL.h>
#include <libultraship/controller/controldeck/ControlDeck.h>

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

namespace MapTracker {

bool showLogicTooltip = false;
bool showMapDebugDetails = false;

namespace {

using namespace UIWidgets;

const std::map<int32_t, const char*> windowTypes = {
    { TRACKER_WINDOW_FLOATING, "Floating" },
    { TRACKER_WINDOW_WINDOW, "Window" },
};

void LoadSettings() {
    showLogicTooltip = CVarGetInteger(CVAR_SHOW_LOGIC, 0) != 0;
    showMapDebugDetails = CVarGetInteger(CVAR_SHOW_DEBUG_INFO, 0) != 0;
}

void ToggleMapTrackerFromController() {
    static bool rightShoulderWasPressed = false;
    bool rightShoulderIsPressed = false;

    auto controlDeck = std::dynamic_pointer_cast<LUS::ControlDeck>(Ship::Context::GetInstance()->GetControlDeck());
    if (controlDeck != nullptr) {
        for (const auto& [instanceId, gamepad] :
             controlDeck->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadsForPort(0)) {
            if (SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0) {
                rightShoulderIsPressed = true;
                break;
            }
        }
    }

    if (rightShoulderIsPressed && !rightShoulderWasPressed) {
        SohGui::ToggleMapTrackerWindow();
    }
    rightShoulderWasPressed = rightShoulderIsPressed;
}

void DrawMapHeader() {
    const float buttonWidth = std::max(1.0f, ImGui::CalcTextSize("Reload Data").x +
                                                    ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f);
    if (UIWidgets::Button("Focus Player", UIWidgets::ButtonOptions().Color(THEME_COLOR).Size({ buttonWidth, 0.0f }))) {
        CheckTracker::UpdateRequestedMapTabFromCurrentArea(true);
    }
    ImGui::SameLine();
    if (UIWidgets::Button("Reload Data", UIWidgets::ButtonOptions().Color(THEME_COLOR).Size({ buttonWidth, 0.0f }))) {
        CheckTracker::LoadMapTrackerData();
    }
    if (CVarGetInteger(CVAR_SHOW_TOTALS, 1) != 0) {
        ImGui::SameLine();
        ImGui::Text("%u Checked / %u Total", CheckTracker::GetTotalChecksGotten(), CheckTracker::GetTotalChecks());
    }
    const float debugWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                             ImGui::CalcTextSize("Debug").x;
    const float rightPadding = ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().ItemSpacing.x + 6.0f;
    const float rightAlignedX = ImGui::GetWindowContentRegionMax().x - debugWidth - rightPadding;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x, rightAlignedX));
    UIWidgets::CVarCheckbox("Debug", CVAR_SHOW_DEBUG_INFO,
                            UIWidgets::CheckboxOptions().Color(THEME_COLOR));
    showMapDebugDetails = CVarGetInteger(CVAR_SHOW_DEBUG_INFO, 0) != 0;
}

} // namespace

void MapTrackerWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    SyncVisibilityConsoleVariable();
}

void MapTrackerWindow::InitElement() {
}

void MapTrackerWindow::UpdateElement() {
}

void MapTrackerWindow::DrawElement() {
    LoadSettings();
    if (CVarGetInteger(CVAR_SHOW_ONLY_PAUSED, 0) != 0 &&
        (gPlayState == nullptr || gPlayState->pauseCtx.state == 0)) {
        return;
    }

    if (!CheckTracker::Trackers::BeginFloatWindows(
            "Map Tracker", mIsVisible, { 0, 0, 0, 255 },
            static_cast<TrackerWindowType>(CVarGetInteger(CVAR_WINDOW_TYPE, TRACKER_WINDOW_WINDOW)),
            CVarGetInteger(CVAR_DRAGGABLE, 1), ImGuiWindowFlags_NoScrollbar)) {
        return;
    }

    if (!GameInteractor::IsSaveLoaded() || !CheckTracker::initialized) {
        ImGui::TextUnformatted("Waiting for file load...");
        CheckTracker::Trackers::EndFloatWindows();
        return;
    }

    ImGui::SetWindowFontScale(CVarGetFloat(CVAR_FONT_SIZE, 1.0f));
    DrawMapHeader();
    if (showMapDebugDetails) {
        const std::string activeAssetsPath = CheckTracker::mapTrackerState.assetsRoot.empty()
                                                 ? CheckTracker::GetMapTrackerAssetsRootAbsoluteString()
                                                 : CheckTracker::mapTrackerState.assetsRoot.string();
        ImGui::TextWrapped("Assets: %s", activeAssetsPath.c_str());
        CheckTracker::DrawCheckTrackerVisibilityDebugControls();
    }
    ImGui::Separator();
    CheckTracker::ProcessPendingUpdates();
    CheckTracker::DrawMapTrackerContent();
    CheckTracker::Trackers::EndFloatWindows();
}

void MapTrackerSettingsWindow::InitElement() {
}

void MapTrackerSettingsWindow::UpdateElement() {
}

void MapTrackerSettingsWindow::DrawElement() {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { 8.0f, 8.0f });
    if (ImGui::BeginTable("MapTrackerSettingsTable", 2, ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersV)) {
        ImGui::TableSetupColumn("General settings", ImGuiTableColumnFlags_WidthStretch, 200.0f);
        ImGui::TableSetupColumn("Map settings", ImGuiTableColumnFlags_WidthStretch, 200.0f);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        UIWidgets::CVarSliderFloat("Font Size", CVAR_FONT_SIZE,
                                   UIWidgets::FloatSliderOptions().Format("%.1f").Step(0.1f).Min(0.3f).Max(2.0f)
                                       .Color(THEME_COLOR).DefaultValue(1.0f));
        UIWidgets::CVarCheckbox("Enable Dragging", CVAR_DRAGGABLE,
                                UIWidgets::CheckboxOptions().Color(THEME_COLOR).DefaultValue(true));
        UIWidgets::CVarCheckbox("Only Enable While Paused", CVAR_SHOW_ONLY_PAUSED,
                                UIWidgets::CheckboxOptions().Color(THEME_COLOR));
        UIWidgets::CVarCheckbox("Show Logic", CVAR_SHOW_LOGIC,
                                UIWidgets::CheckboxOptions()
                                    .Tooltip("Show a check's logic when hovering over it on the map.")
                                    .Color(THEME_COLOR));
        UIWidgets::CVarCheckbox("Show Check Totals", CVAR_SHOW_TOTALS,
                                UIWidgets::CheckboxOptions().Color(THEME_COLOR).DefaultValue(true));

        ImGui::TableNextColumn();
        UIWidgets::CVarCombobox("Window Type", CVAR_WINDOW_TYPE, windowTypes,
                                UIWidgets::ComboboxOptions().Color(THEME_COLOR).DefaultIndex(TRACKER_WINDOW_WINDOW));
        UIWidgets::CVarCheckbox("Reveal Unspoiled Dungeon Checks", CVAR_MQ_SPOILERS,
                                UIWidgets::CheckboxOptions()
                                    .Tooltip("Show dungeon checks before the dungeon has been spoiled, including MQ dungeons.")
                                    .Color(THEME_COLOR));
        UIWidgets::CVarCheckbox("Show Debug Information", CVAR_SHOW_DEBUG_INFO,
                                UIWidgets::CheckboxOptions().Color(THEME_COLOR));

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void RegisterMapTrackerWidgets() {
    WidgetInfo showLogicWidget = { .name = "Show Logic", .type = WidgetType::WIDGET_CVAR_CHECKBOX };
    showLogicWidget.CVar(CVAR_SHOW_LOGIC)
        .Options(CheckboxOptions().Color(THEME_COLOR).Tooltip("Show check logic in Map Tracker tooltips."));
    SohGui::GetSohMenu()->AddSearchWidget({ showLogicWidget, "Randomizer", "Map Tracker", "General Settings" });
}

static RegisterMenuInitFunc menuInitFunc(RegisterMapTrackerWidgets);
static RegisterShipInitFunc inputHookInitFunc([] {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(ToggleMapTrackerFromController);
});

} // namespace MapTracker
