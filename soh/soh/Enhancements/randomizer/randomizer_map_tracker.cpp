#include "randomizer_map_tracker.h"

#include "map_tracker_internal.h"
#include "map_tracker_gameplay.h"
#include "map_tracker_pause.h"
#include "randomizer_check_tracker.h"
#include "check_tracker_visibility_debug.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <spdlog/fmt/fmt.h>
#include <map>
#include <string_view>
#include <vector>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <ship/window/Window.h>

extern "C" {
#include "global.h"
extern PlayState* gPlayState;
}

namespace MapTracker {
bool renderingNativePause = false;
bool screenMapInputBlocked = false;
bool IsIntegratedMapView() {
    return renderingNativePause || NativePauseInteractive() || CVarGetInteger(CVAR_SCREEN, 0) != 0;
}

bool showLogicTooltip = false;
bool showMapDebugDetails = false;
bool screenConfirmPressed = false;
bool screenBackPressed = false;
bool screenMousePressed = false;
bool screenFocusPressed = false;
bool screenOverworldPressed = false;
bool screenZoomInPressed = false;
bool screenZoomOutPressed = false;
int screenMapListNavigationDelta = 0;
bool screenCursorUsesController = false;
bool screenCursorNeedsCenter = true;
float screenCursorDeltaX = 0.0f;
float screenCursorDeltaY = 0.0f;
float screenPanDeltaX = 0.0f;
float screenPanDeltaY = 0.0f;
float screenZoomDelta = 0.0f;
ImVec2 screenCursorPosition = ImVec2(-1.0f, -1.0f);

bool IsScreenCursorOverRect(const ImVec2& min, const ImVec2& max) {
    return IsIntegratedMapView() && screenCursorPosition.x >= min.x &&
           screenCursorPosition.x <= max.x && screenCursorPosition.y >= min.y && screenCursorPosition.y <= max.y;
}

namespace {

using namespace UIWidgets;

const std::map<int32_t, const char*> windowTypes = {
    { TRACKER_WINDOW_FLOATING, "Floating" },
    { TRACKER_WINDOW_WINDOW, "Window" },
};
const std::map<int32_t, const char*> screenModes = {
    { 0, "Fullscreen 2D" },
    { 1, "Native Pause 3D" },
};

bool IsKeyboardMappingPressed(const std::string& mappingId, int32_t pressedScancode) {
    constexpr std::string_view keyboardMappingMarker = "-KB";
    const size_t markerPosition = mappingId.rfind(keyboardMappingMarker);
    if (markerPosition == std::string::npos || pressedScancode < 0) {
        return false;
    }

    const char* scancodeStart = mappingId.data() + markerPosition + keyboardMappingMarker.size();
    int32_t mappedScancode = -1;
    const auto [parseEnd, error] = std::from_chars(scancodeStart, mappingId.data() + mappingId.size(), mappedScancode);
    return error == std::errc{} && parseEnd == mappingId.data() + mappingId.size() &&
           mappedScancode == pressedScancode;
}

std::string GetMapTrackerButtonLabel(CONTROLLERBUTTONS_T bitmask, Ship::PhysicalDeviceType deviceType) {
    const auto controlDeck = std::dynamic_pointer_cast<LUS::ControlDeck>(Ship::Context::GetInstance()->GetControlDeck());
    const auto controller = controlDeck == nullptr ? nullptr : controlDeck->GetControllerByPort(0);
    if (controller == nullptr) {
        return "Unbound";
    }

    std::vector<std::string> names;
    for (const auto& [id, mapping] : controller->GetButton(bitmask)->GetAllButtonMappings()) {
        (void)id;
        if (mapping->GetPhysicalDeviceType() != deviceType) {
            continue;
        }
        const std::string name = mapping->GetPhysicalInputName();
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    if (names.empty()) {
        return "Unbound";
    }

    std::string label;
    const size_t displayedNames = std::min<size_t>(names.size(), 3);
    for (size_t i = 0; i < displayedNames; ++i) {
        if (!label.empty()) {
            label += " / ";
        }
        label += names[i];
    }
    if (names.size() > displayedNames) {
        label += " / ...";
    }
    return label;
}

struct ScreenInputState {
    std::unordered_map<CONTROLLERBUTTONS_T, bool> actionWasPressed;
    int repeatingListDirection = 0;
    float listRepeatTimer = 0.0f;
    int repeatingZoomDirection = 0;
    float zoomRepeatTimer = 0.0f;
    float cursorAcceleration = 1.0f;
};

ScreenInputState screenInput;
bool screenClosedForGameplay = false;
uint16_t suppressedGameplayButtons = 0;
bool screenOpenedFromPause = false;
std::chrono::steady_clock::time_point nextScreenToggleTime;
bool screenClosing = false;
std::chrono::steady_clock::time_point screenClosingTime;
constexpr float SCREEN_CLOSE_DURATION = 0.18f;
// Percentage of the screen reserved on each side of the standalone panel.
constexpr float SCREEN_HORIZONTAL_MARGIN_PERCENT = 6.0f;
constexpr float SCREEN_VERTICAL_MARGIN_PERCENT = 5.5f;
// Native main-panel opacity: 0 = transparent, 1 = opaque. Does not affect lists or hover panels.
constexpr float NATIVE_PAUSE_BACKGROUND_OPACITY = 0.97f;

int RepeatZoomInput(int direction, float deltaTime) {
    if (direction == 0) {
        screenInput.repeatingZoomDirection = 0;
        screenInput.zoomRepeatTimer = 0.0f;
        return 0;
    }
    if (direction != screenInput.repeatingZoomDirection) {
        screenInput.repeatingZoomDirection = direction;
        screenInput.zoomRepeatTimer = 0.3f;
        return direction;
    }
    screenInput.zoomRepeatTimer -= std::clamp(deltaTime, 0.0f, 0.25f);
    int steps = 0;
    while (screenInput.zoomRepeatTimer <= 0.0f && steps < 3) {
        screenInput.zoomRepeatTimer += 0.12f;
        ++steps;
    }
    return steps * direction;
}

void ResetScreenInput() {
    screenInput = {};
    screenCursorDeltaX = screenCursorDeltaY = 0.0f;
    screenPanDeltaX = screenPanDeltaY = screenZoomDelta = 0.0f;
    screenConfirmPressed = screenBackPressed = screenFocusPressed = screenOverworldPressed = false;
    screenZoomInPressed = screenZoomOutPressed = false;
    screenMapListNavigationDelta = 0;
}

bool CanOpenScreen() {
    const bool pausedMenuReady = gPlayState != nullptr && gPlayState->pauseCtx.state == 6 &&
                                 gPlayState->pauseCtx.debugState == 0 &&
                                 gPlayState->gameOverCtx.state == GAMEOVER_INACTIVE &&
                                 gPlayState->transitionTrigger == TRANS_TRIGGER_OFF &&
                                 gPlayState->transitionMode == TRANS_MODE_OFF;
    return GameInteractor::IsSaveLoaded() && gPlayState->state.running && gTrnsnUnkState != 3 &&
           gPlayState->msgCtx.msgMode == MSGMODE_NONE && (KaleidoSetup_CanPause(gPlayState) || pausedMenuReady);
}

void SetScreenOpen(bool opening, bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (opening == (CVarGetInteger(CVAR_SCREEN, 0) != 0) || (opening && !CanOpenScreen()) ||
        (!force && (screenClosing || now < nextScreenToggleTime))) {
        return;
    }
    ResetScreenInput();
    if (!opening && !force) {
        // Keep visibility (and the gameplay/input lock) until the exit animation finishes.
        screenClosing = true;
        screenClosingTime = now;
        return;
    }
    screenClosing = false;
    if (opening) {
        screenOpenedFromPause = gPlayState->pauseCtx.state != 0;
    }
    SohGui::ToggleMapTrackerScreenWindow();
    nextScreenToggleTime = now + std::chrono::milliseconds(500);
    screenClosedForGameplay = !opening;
    // The vanilla pause menu already owns its audio state; leave it paused when returning to it.
    if (!screenOpenedFromPause) {
        func_800F64E0(opening ? 1 : 0);
    }
    if (!opening) {
        screenOpenedFromPause = false;
    }
    if (opening) {
        CheckTracker::UpdateRequestedMapTabFromCurrentArea(true);
    }
}

void LoadSettings() {
    showLogicTooltip = CVarGetInteger(CVAR_SHOW_LOGIC, 0) != 0;
    showMapDebugDetails = CVarGetInteger(CVAR_SHOW_DEBUG_INFO, 0) != 0;
}

void ToggleMapTrackerFromInput() {
    auto controlDeck = std::dynamic_pointer_cast<LUS::ControlDeck>(Ship::Context::GetInstance()->GetControlDeck());
    if (controlDeck == nullptr || controlDeck->GetControllerByPort(0) == nullptr) {
        return;
    }

    static bool toggleWasPressed = false;
    static bool defaultMappingsInitialized = false;
    const auto trackerButton = controlDeck->GetControllerByPort(0)->GetButton(BTN_CUSTOM_MAP_TRACKER);
    if (!defaultMappingsInitialized) {
        const std::array<CONTROLLERBUTTONS_T, 9> mapTrackerButtons = {
            BTN_CUSTOM_MAP_TRACKER, BTN_CUSTOM_MAP_TRACKER_OVERWORLD, BTN_CUSTOM_MAP_TRACKER_CONFIRM,
            BTN_CUSTOM_MAP_TRACKER_BACK, BTN_CUSTOM_MAP_TRACKER_FOCUS, BTN_CUSTOM_MAP_TRACKER_ZOOM_IN,
            BTN_CUSTOM_MAP_TRACKER_ZOOM_OUT,
            BTN_CUSTOM_MAP_TRACKER_LIST_UP, BTN_CUSTOM_MAP_TRACKER_LIST_DOWN,
        };
        for (const CONTROLLERBUTTONS_T bitmask : mapTrackerButtons) {
            const auto button = controlDeck->GetControllerByPort(0)->GetButton(bitmask);
            for (const Ship::PhysicalDeviceType deviceType : { Ship::PhysicalDeviceType::Keyboard,
                                                               Ship::PhysicalDeviceType::SDLGamepad }) {
                if (!button->HasMappingsForPhysicalDeviceType(deviceType)) {
                    button->AddDefaultMappings(deviceType);
                }
            }
        }
        defaultMappingsInitialized = true;
    }

    const auto trackerMappings = trackerButton->GetAllButtonMappings();
    const int32_t pressedScancode = Ship::Context::GetInstance()->GetWindow()->GetLastScancode();
    const bool keyboardPressed = std::any_of(trackerMappings.begin(), trackerMappings.end(), [pressedScancode](const auto& mappingEntry) {
        return IsKeyboardMappingPressed(mappingEntry.first, pressedScancode);
    });
    CONTROLLERBUTTONS_T toggleButtons = 0;
    trackerButton->UpdatePad(toggleButtons);
    const bool toggleIsPressed = keyboardPressed || (toggleButtons & BTN_CUSTOM_MAP_TRACKER) != 0;
    static bool nativeShoulderWasPressed = false;
    bool nativeShoulderPressed = false;
    if (NativePauseEnabled() && !controlDeck->GamepadGameInputBlocked()) {
        for (const auto& [id, gamepad] :
             controlDeck->GetConnectedPhysicalDeviceManager()->GetConnectedSDLGamepadsForPort(0)) {
            nativeShoulderPressed |= SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
        }
        if (gPlayState->pauseCtx.state != 0 && nativeShoulderPressed && !nativeShoulderWasPressed) {
            SetNativePauseShoulderInput(true);
        }
    }
    nativeShoulderWasPressed = nativeShoulderPressed;

    if (toggleIsPressed && !toggleWasPressed) {
        const bool opening = CVarGetInteger(CVAR_SCREEN, 0) == 0;
        const bool controllerPressed =
            std::any_of(trackerMappings.begin(), trackerMappings.end(), [](const auto& entry) {
                if (entry.second->GetPhysicalDeviceType() != Ship::PhysicalDeviceType::SDLGamepad) {
                    return false;
                }
                CONTROLLERBUTTONS_T buttons = 0;
                entry.second->UpdatePad(buttons);
                return (buttons & BTN_CUSTOM_MAP_TRACKER) == BTN_CUSTOM_MAP_TRACKER;
            });
        if (NativePauseEnabled()) {
            if (gPlayState->pauseCtx.state != 0 && nativeShoulderPressed && !keyboardPressed) {
                // Even if rebound to Toggle, RB belongs to native page navigation while paused.
            } else if (NativePauseInteractive()) {
                RequestNativePauseClose();
            } else if (CanOpenScreen()) {
                RequestNativePauseMap(controllerPressed && !keyboardPressed);
            }
        } else {
            SetScreenOpen(opening);
        }
        if (opening && CVarGetInteger(CVAR_SCREEN, 0) != 0) {
            // Set the source after opening, which resets cursor state. Mouse/stick movement takes over later.
            screenCursorUsesController = controllerPressed && !keyboardPressed;
        }
    }
    toggleWasPressed = toggleIsPressed;
}

void CaptureScreenInput() {
    const auto controlDeck = std::dynamic_pointer_cast<LUS::ControlDeck>(Ship::Context::GetInstance()->GetControlDeck());
    const auto controller = controlDeck == nullptr ? nullptr : controlDeck->GetControllerByPort(0);
    const int32_t pressedScancode = Ship::Context::GetInstance()->GetWindow()->GetLastScancode();
    const auto actionHeld = [&](CONTROLLERBUTTONS_T bitmask) {
        if (controller == nullptr) {
            return false;
        }
        const auto button = controller->GetButton(bitmask);
        const auto mappings = button->GetAllButtonMappings();
        const bool keyboardHeld = std::any_of(mappings.begin(), mappings.end(), [&](const auto& entry) {
            return IsKeyboardMappingPressed(entry.first, pressedScancode);
        });
        // Read actions separately because composite custom bitmasks overlap other actions.
        CONTROLLERBUTTONS_T actionButtons = 0;
        button->UpdatePad(actionButtons);
        const bool padHeld = (actionButtons & bitmask) == bitmask;
        return keyboardHeld || padHeld;
    };
    const auto actionPressed = [&](CONTROLLERBUTTONS_T bitmask) {
        const bool held = actionHeld(bitmask);
        const bool pressed = held && !screenInput.actionWasPressed[bitmask];
        screenInput.actionWasPressed[bitmask] = held;
        return pressed;
    };

    screenConfirmPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_CONFIRM);
    screenBackPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_BACK);
    screenFocusPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_FOCUS);
    screenOverworldPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_OVERWORLD);
    screenZoomInPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_ZOOM_IN);
    screenZoomOutPressed = actionPressed(BTN_CUSTOM_MAP_TRACKER_ZOOM_OUT);
    const bool listUpHeld = actionHeld(BTN_CUSTOM_MAP_TRACKER_LIST_UP);
    const bool listDownHeld = actionHeld(BTN_CUSTOM_MAP_TRACKER_LIST_DOWN);
    const int listDirection = listUpHeld != listDownHeld ? (listUpHeld ? -1 : 1) : 0;
    screenMapListNavigationDelta = 0;
    if (listDirection == 0) {
        screenInput.repeatingListDirection = 0;
        screenInput.listRepeatTimer = 0.0f;
    } else if (listDirection != screenInput.repeatingListDirection) {
        screenInput.repeatingListDirection = listDirection;
        screenInput.listRepeatTimer = 0.16f;
        screenMapListNavigationDelta = listDirection;
    } else {
        screenInput.listRepeatTimer -= ImGui::GetIO().DeltaTime;
        int repeatedSteps = 0;
        while (screenInput.listRepeatTimer <= 0.0f && repeatedSteps < 8) {
            screenMapListNavigationDelta += listDirection;
            screenInput.listRepeatTimer += 0.036f;
            ++repeatedSteps;
        }
    }
    if (NativePauseInteractive()) {
        const bool zoomInHeld = actionHeld(BTN_CUSTOM_MAP_TRACKER_ZOOM_IN);
        const bool zoomOutHeld = actionHeld(BTN_CUSTOM_MAP_TRACKER_ZOOM_OUT);
        const int zoomDirection = zoomInHeld != zoomOutHeld ? (zoomInHeld ? 1 : -1) : 0;
        screenZoomDelta = static_cast<float>(RepeatZoomInput(zoomDirection, ImGui::GetIO().DeltaTime));
    } else {
        screenZoomDelta = screenZoomInPressed ? 1.0f : screenZoomOutPressed ? -1.0f : 0.0f;
    }
    const auto shapeStickAxis = [](int8_t value) {
        constexpr float deadzone = 8.0f;
        const float magnitude = std::abs(static_cast<float>(value));
        if (magnitude <= deadzone) {
            return 0.0f;
        }
        const float normalized = (magnitude - deadzone) / (127.0f - deadzone);
        return std::copysign(0.08f + 0.92f * normalized * normalized, static_cast<float>(value));
    };
    int8_t stickX = 0, stickY = 0, panX = 0, panY = 0;
    if (controller != nullptr) {
        controller->GetLeftStick()->UpdatePad(stickX, stickY);
        controller->GetRightStick()->UpdatePad(panX, panY);
    }
    screenCursorDeltaX = shapeStickAxis(stickX);
    screenCursorDeltaY = -shapeStickAxis(stickY);
    screenPanDeltaX = shapeStickAxis(panX);
    screenPanDeltaY = -shapeStickAxis(panY);
    if (screenCursorDeltaX != 0.0f || screenCursorDeltaY != 0.0f) {
        screenCursorUsesController = true;
    }

    if (screenBackPressed && !CheckTracker::NavigateBackMapTrackerScreen()) {
        if (NativePauseInteractive())
            RequestNativePauseClose();
        else
            SetScreenOpen(false);
    }
    if (screenOverworldPressed) {
        CheckTracker::NavigateMapTrackerScreenToOverworld();
    }
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
        const auto totals = CheckTracker::GetMapTrackerCheckTotals();
        ImGui::Text("%u Checked / %u Accessible / %u Total", totals.checked, totals.accessible, totals.total);
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
    // Both views share availability data; hiding the classic view must not disable the fullscreen view.
    CheckTracker::SetMapTrackerAvailabilityRequested(IsVisible() || CVarGetInteger(CVAR_SCREEN, 0) != 0 ||
                                                     NativePauseInteractive());
    // Both views share navigation and popup state. Do not let classic tabs consume native requests.
    if (!IsVisible() || (NativePauseEnabled() && gPlayState->pauseCtx.state != 0)) {
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

void MapTrackerScreenWindow::Draw() {
    const bool nativePage = NativePauseEnabled();
    static bool wasVisible = false;
    static std::chrono::steady_clock::time_point openingTime;
    const bool nativePreview = nativePage && !NativePauseInteractive();
    if (nativePage ? !NativePauseInteractive() && !NativePauseNeedsPreview() : !IsVisible()) {
        nativeMapListVisible = false;
        screenMapInputBlocked = false;
        if (nativePage && wasVisible)
            CheckTracker::InvalidateMapTrackerRenderCache();
        wasVisible = false;
        return;
    }
    if (nativePreview)
        nativeMapListVisible = false;
    const auto now = std::chrono::steady_clock::now();
    if (!wasVisible) {
        openingTime = now;
        wasVisible = true;
    }
    const float progress = std::clamp(std::chrono::duration<float>(now - openingTime).count() / 0.5f, 0.0f, 1.0f);
    const float easedProgress = 1.0f - std::pow(1.0f - progress, 3.0f);

    CheckTracker::SetMapTrackerAvailabilityRequested(true);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 margin(viewport->Size.x * SCREEN_HORIZONTAL_MARGIN_PERCENT / 100.0f,
                        viewport->Size.y * SCREEN_VERTICAL_MARGIN_PERCENT / 100.0f);
    const float openingOffset = std::min(24.0f, margin.y * 0.75f) * (1.0f - easedProgress);
    const float closingProgress =
        screenClosing
            ? std::clamp(std::chrono::duration<float>(now - screenClosingTime).count() / SCREEN_CLOSE_DURATION, 0.0f,
                         1.0f)
            : 0.0f;
    const float closingEase = closingProgress * closingProgress;
    const ImVec2 panelSize = viewport->Size - margin * 2.0f;
    const ImVec2 closingInset = panelSize * (0.04f * closingEase);
    ImVec2 nativePosition, nativeSize;
    if (nativePage && !GetNativePauseBounds(nativePosition, nativeSize))
        return;
    ImGui::SetNextWindowPos(nativePage ? nativePosition
                                       : viewport->Pos + margin + closingInset +
                                             ImVec2(0.0f, openingOffset + 32.0f * closingEase),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(nativePage ? nativeSize : panelSize - closingInset * 2.0f, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, nativePage ? ImVec2(6.0f, 14.0f) : ImVec2(24.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, nativePage ? 0.0f : 18.0f);
    if (nativePage) {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, nativePage ? ImVec4(0.24f, 0.065f, 0.055f, NATIVE_PAUSE_BACKGROUND_OPACITY)
                                                        : ImVec4(0.035f, 0.045f, 0.055f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          nativePage ? ImVec4(0.19f, 0.045f, 0.04f, 1.0f) : ImVec4(0.06f, 0.07f, 0.075f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          nativePage ? ImVec4(0.64f, 0.30f, 0.22f, 0.85f) : ImVec4(0.55f, 0.41f, 0.19f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.90f, 0.83f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,
                          nativePage ? ImVec4(0.31f, 0.095f, 0.075f, 0.98f) : ImVec4(0.14f, 0.15f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          nativePage ? ImVec4(0.46f, 0.16f, 0.12f, 1.0f) : ImVec4(0.31f, 0.25f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          nativePage ? ImVec4(0.57f, 0.23f, 0.16f, 1.0f) : ImVec4(0.48f, 0.35f, 0.16f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   (screenClosing || nativePreview ? ImGuiWindowFlags_NoInputs : 0);
    if (ImGui::Begin("Map Tracker Screen##fullscreen", nullptr, flags)) {
        renderingNativePause = nativePage;
        DrawElement();
        renderingNativePause = false;
    }
    ImGui::End();
    if (nativePage)
        CaptureNativePausePage(ImGui::FindWindowByName("Map Tracker Screen##fullscreen"));
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(nativePage ? 6 : 3);
    SyncVisibilityConsoleVariable();
}

void MapTrackerScreenWindow::InitElement() {
}

void MapTrackerScreenWindow::UpdateElement() {
    if (NativePauseEnabled()) {
        if (IsVisible())
            SetScreenOpen(false, true);
        static bool wasInteractive = false;
        ToggleMapTrackerFromInput();
        const bool interactive = NativePauseInteractive();
        if (interactive && !wasInteractive) {
            ResetScreenInput();
        }
        if (interactive)
            CaptureScreenInput();
        else
            ResetScreenInput();
        wasInteractive = interactive;
        return;
    }
    if (IsVisible() && !CanOpenScreen()) {
        SetScreenOpen(false, true);
    }
    if (screenClosing && std::chrono::duration<float>(std::chrono::steady_clock::now() - screenClosingTime).count() >=
                             SCREEN_CLOSE_DURATION) {
        SetScreenOpen(false, true);
    }
    ToggleMapTrackerFromInput();
    if (IsVisible() && !screenClosing) {
        CaptureScreenInput();
    }
}

void MapTrackerScreenWindow::DrawElement() {
    LoadSettings();
    const bool nativePreview = renderingNativePause && !NativePauseInteractive();
    screenMousePressed = !screenClosing && !nativePreview && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    CheckTracker::ProcessPendingUpdates();
    if (renderingNativePause)
        ApplyNativePauseFocus();
    if (!GameInteractor::IsSaveLoaded() || !CheckTracker::initialized) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 80.0f);
        ImGui::TextUnformatted("Waiting for file load...");
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (!nativePreview &&
        (screenMousePressed || ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) {
        screenCursorUsesController = false;
        screenCursorPosition = ImGui::GetIO().MousePos;
    }
    if (!nativePreview && (screenCursorNeedsCenter || screenCursorPosition.x < 0.0f || screenCursorPosition.y < 0.0f)) {
        screenCursorPosition =
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
        screenCursorNeedsCenter = false;
    }
    const bool cursorIsMoving = screenCursorDeltaX != 0.0f || screenCursorDeltaY != 0.0f;
    screenInput.cursorAcceleration =
        cursorIsMoving ? std::min(3.6f, screenInput.cursorAcceleration + ImGui::GetIO().DeltaTime * 1.5f) : 1.0f;
    const float cursorSpeed = 850.0f * screenInput.cursorAcceleration * ImGui::GetIO().DeltaTime;
    const ImVec2 screenMin = ImGui::GetWindowPos();
    const ImVec2 screenMax = screenMin + ImGui::GetWindowSize();
    if (screenCursorUsesController) {
        const ImVec2 cursorMin = screenMin;
        const ImVec2 cursorMax = screenMax;
        screenCursorPosition.x = std::clamp(screenCursorPosition.x + screenCursorDeltaX * cursorSpeed,
                                            cursorMin.x + 18.0f, cursorMax.x - 18.0f);
        screenCursorPosition.y = std::clamp(screenCursorPosition.y + screenCursorDeltaY * cursorSpeed,
                                            cursorMin.y + 18.0f, cursorMax.y - 18.0f);
    }
    if (renderingNativePause) {
        screenCursorPosition.x = std::clamp(screenCursorPosition.x, screenMin.x + 16.0f, screenMax.x - 16.0f);
        screenCursorPosition.y = std::clamp(screenCursorPosition.y, screenMin.y + 16.0f, screenMax.y - 16.0f);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!renderingNativePause) {
        drawList->AddRect(screenMin + ImVec2(12.0f, 12.0f), screenMax - ImVec2(12.0f, 12.0f),
                          IM_COL32(186, 144, 66, 150), 8.0f, 0, 1.5f);
    }

    const float nativeFontScale =
        renderingNativePause ? std::clamp(ImGui::GetWindowSize().x / 1100.0f, 0.55f, 1.0f) : 1.0f;
    ImGui::SetWindowFontScale(std::max(0.3f, CVarGetFloat(CVAR_FONT_SIZE, 1.0f)) * 1.05f * nativeFontScale);
    const bool showTotals = CVarGetInteger(CVAR_SHOW_TOTALS, 1) != 0;
    if (renderingNativePause) {
        std::string label;
        if (showTotals) {
            const auto totals = CheckTracker::GetMapTrackerCheckTotals();
            label =
                fmt::format("{} Checked / {} Accessible / {} Total", totals.checked, totals.accessible, totals.total);
        }
        const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
        const float centerX = (ImGui::GetWindowContentRegionMin().x + ImGui::GetWindowContentRegionMax().x) * 0.5f;
        const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
        const ImVec2 titlePosition = rowPosition + ImVec2(ImGui::CalcTextSize(" ").x, 0.0f);
        const float titleSize = ImGui::GetFontSize() * 1.10f;
        const float titleRight = showTotals ? screenMin.x + centerX - textWidth * 0.5f - 12.0f
                                            : screenMin.x + ImGui::GetWindowContentRegionMax().x;
        const ImVec4 titleClip(titlePosition.x, titlePosition.y, titleRight, titlePosition.y + titleSize);
        const auto& state = CheckTracker::mapTrackerState;
        const char* title =
            state.tabs.empty()
                ? "MAP TRACKER"
                : state.tabs[std::clamp(state.selectedTabIndex, 0, static_cast<int>(state.tabs.size()) - 1)]
                      .mapName.c_str();
        if (titleClip.z > titleClip.x) {
            // Embolden the existing font without adding another font atlas.
            for (float offset : { 0.0f, 0.6f })
                drawList->AddText(ImGui::GetFont(), titleSize, titlePosition + ImVec2(offset, 0.0f),
                                  IM_COL32(224, 179, 92, 255), title, nullptr, 0.0f, &titleClip);
        }
        if (showTotals)
            drawList->AddText(ImVec2(screenMin.x + centerX - textWidth * 0.5f,
                                     rowPosition.y + (titleSize - ImGui::GetFontSize()) * 0.5f),
                              ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        ImGui::Dummy(ImVec2(0.0f, titleSize));
    } else {
        ImGui::TextColored(ImVec4(0.88f, 0.70f, 0.36f, 1.0f), "MAP TRACKER");
        ImGui::SameLine();
        ImGui::TextDisabled("/ %s",
                            CheckTracker::mapTrackerState.tabs.empty()
                                ? "Loading map data"
                                : CheckTracker::mapTrackerState
                                      .tabs[std::clamp(CheckTracker::mapTrackerState.selectedTabIndex, 0,
                                                       static_cast<int>(CheckTracker::mapTrackerState.tabs.size()) - 1)]
                                      .mapName.c_str());
        if (showTotals) {
            const auto totals = CheckTracker::GetMapTrackerCheckTotals();
            const std::string label =
                fmt::format("{} Checked / {} Accessible / {} Total", totals.checked, totals.accessible, totals.total);
            const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x,
                                     ImGui::GetWindowContentRegionMax().x - textWidth));
            ImGui::TextUnformatted(label.c_str());
        }
    }
    if (screenFocusPressed) {
        CheckTracker::UpdateRequestedMapTabFromCurrentArea(true);
        if (!renderingNativePause)
            screenCursorNeedsCenter = true;
    }
    if (screenMapListNavigationDelta != 0 && !CheckTracker::mapTrackerState.tabs.empty()) {
        const int lastIndex = static_cast<int>(CheckTracker::mapTrackerState.tabs.size()) - 1;
        const int nextIndex =
            std::clamp(CheckTracker::mapTrackerState.selectedTabIndex + screenMapListNavigationDelta, 0, lastIndex);
        CheckTracker::SelectMapTrackerScreenMap(nextIndex);
    }
    ImGui::Separator();

    const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.0f +
                               ImGui::GetStyle().WindowPadding.y + 8.0f;
    const float sidebarWidth = std::clamp(ImGui::GetWindowSize().x * 0.18f, 190.0f, 270.0f);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x,
                            std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight));
    if (renderingNativePause) {
        const bool cursorAtLeftEdge = !nativePreview && screenCursorPosition.x <= screenMin.x + 24.0f;
        if (screenMapListNavigationDelta != 0 || cursorAtLeftEdge)
            nativeMapListVisible = true;
        const bool cursorMoved = previousScreenCursorPosition.x != screenCursorPosition.x ||
                                 previousScreenCursorPosition.y != screenCursorPosition.y;
        if (nativeMapListVisible && cursorMoved && screenMapListNavigationDelta == 0 &&
            IsScreenCursorOverRect(canvasOrigin, canvasOrigin + canvasSize) &&
            !IsScreenCursorOverRect(canvasOrigin, canvasOrigin + ImVec2(sidebarWidth, canvasSize.y)))
            nativeMapListVisible = false;
        if (nativeMapListVisible && screenConfirmPressed &&
            !IsScreenCursorOverRect(canvasOrigin, canvasOrigin + ImVec2(sidebarWidth, canvasSize.y))) {
            nativeMapListVisible = false;
            screenConfirmPressed = false;
        }
    }
    previousScreenCursorPosition = screenCursorPosition;
    const bool showSidebar = !renderingNativePause || nativeMapListVisible;
    // While the list is visible, it owns input; map markers underneath must not react through it.
    screenMapInputBlocked = renderingNativePause && (nativePreview || showSidebar);
    auto drawCanvas = [&]() {
        ImGui::BeginChild("MapTrackerScreenMapCanvas", ImVec2(0.0f, -footerHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              (renderingNativePause ? ImGuiWindowFlags_NoBackground : 0));
        CheckTracker::DrawMapTrackerContent(false);
        ImGui::EndChild();
    };
    ImVec2 afterCanvas;
    if (renderingNativePause) {
        drawCanvas();
        afterCanvas = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(canvasOrigin);
    }
    if (showSidebar) {
        // Native list is painted after the full-width canvas, without participating in its layout.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, renderingNativePause ? ImVec4(0.19f, 0.045f, 0.04f, 1.0f)
                                                                     : ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
        ImGui::BeginChild("MapTrackerScreenMapList", ImVec2(sidebarWidth, canvasSize.y), true);
        ImGui::TextColored(ImVec4(0.88f, 0.70f, 0.36f, 1.0f), "MAPS");
        ImGui::Separator();
        const ImVec2 mapListMin = ImGui::GetWindowPos();
        const ImVec2 mapListMax = mapListMin + ImGui::GetWindowSize();
        if (screenPanDeltaY != 0.0f && IsScreenCursorOverRect(mapListMin, mapListMax)) {
            const float scrollSpeed = 1800.0f * ImGui::GetIO().DeltaTime;
            ImGui::SetScrollY(ImGui::GetScrollY() + screenPanDeltaY * scrollSpeed);
        }
        const auto playerMapId = CheckTracker::ResolvePreferredMapTabIdForArea(CheckTracker::currentArea);
        std::string previousGroup;
        for (size_t mapIndex = 0; mapIndex < CheckTracker::mapTrackerState.tabs.size(); ++mapIndex) {
            const CheckTracker::MapTabData& tab = CheckTracker::mapTrackerState.tabs[mapIndex];
            if (tab.groupName != previousGroup) {
                if (!previousGroup.empty()) {
                    ImGui::Spacing();
                }
                ImGui::TextDisabled("%s", tab.groupName.empty() ? "Other" : tab.groupName.c_str());
                previousGroup = tab.groupName;
            }
            const bool selected = static_cast<int>(mapIndex) == CheckTracker::mapTrackerState.selectedTabIndex;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(105, 77, 35, 220));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(133, 99, 46, 230));
            }
            const ImVec2 buttonStart = ImGui::GetCursorScreenPos();
            const bool playerIsOnMap = playerMapId.has_value() && tab.mapId == *playerMapId;
            const std::string label = fmt::format("{}{}##MapList{}", playerIsOnMap ? "   " : "", tab.mapName, mapIndex);
            const bool controllerHover = screenCursorUsesController && !nativePreview;
            if (controllerHover) {
                const ImVec2 buttonSize = ImGui::CalcItemSize(ImVec2(-1.0f, 36.0f), 0.0f, 0.0f);
                const bool hovered = IsScreenCursorOverRect(buttonStart, buttonStart + buttonSize) &&
                                     ImGui::GetCurrentWindow()->InnerClipRect.Contains(screenCursorPosition);
                const ImVec4 color = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
                // ImGui still tracks the mouse; controller rows must use only the custom cursor for their tint.
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
            }
            const bool pressed = ImGui::Button(label.c_str(), ImVec2(-1.0f, 36.0f));
            if (controllerHover)
                ImGui::PopStyleColor(3);
            if (screenMapListNavigationDelta != 0 &&
                static_cast<int>(mapIndex) == CheckTracker::mapTrackerState.selectedTabIndex) {
                ImGui::SetScrollHereY(0.5f);
            }
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            const bool rowActivated = !screenClosing && (pressed || ((screenConfirmPressed || screenMousePressed) &&
                                                                     IsScreenCursorOverRect(rowMin, rowMax) &&
                                                                     IsScreenCursorOverRect(mapListMin, mapListMax)));
            if (rowActivated) {
                if (!selected)
                    CheckTracker::SelectMapTrackerScreenMap(static_cast<int>(mapIndex));
                if (renderingNativePause) {
                    // The list must process Select before it is dismissed, and own the entire press.
                    nativeMapListVisible = false;
                    screenConfirmPressed = screenMousePressed = false;
                }
            }
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(buttonStart.x + 1.0f, rowMin.y + 2.0f), ImVec2(buttonStart.x + 3.0f, rowMax.y - 2.0f),
                CheckTracker::GetMapTrackerScreenMapStatusColor(static_cast<int>(mapIndex)));
            if (playerIsOnMap) {
                ImDrawList* listDraw = ImGui::GetWindowDrawList();
                const ImVec2 personCenter(buttonStart.x + 17.0f, (rowMin.y + rowMax.y) * 0.5f);
                const ImU32 personColor = IM_COL32(245, 205, 112, 255);
                listDraw->AddCircleFilled(ImVec2(personCenter.x, personCenter.y - 6.0f), 2.5f, personColor, 12);
                listDraw->AddLine(ImVec2(personCenter.x, personCenter.y - 3.0f),
                                  ImVec2(personCenter.x, personCenter.y + 3.0f), personColor, 2.0f);
                listDraw->AddLine(ImVec2(personCenter.x - 4.0f, personCenter.y - 1.0f),
                                  ImVec2(personCenter.x + 4.0f, personCenter.y - 1.0f), personColor, 1.5f);
                listDraw->AddLine(ImVec2(personCenter.x, personCenter.y + 3.0f),
                                  ImVec2(personCenter.x - 3.0f, personCenter.y + 7.0f), personColor, 1.5f);
                listDraw->AddLine(ImVec2(personCenter.x, personCenter.y + 3.0f),
                                  ImVec2(personCenter.x + 3.0f, personCenter.y + 7.0f), personColor, 1.5f);
            }
            if (selected) {
                ImGui::PopStyleColor(2);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (!renderingNativePause)
            ImGui::SameLine();
    }
    if (renderingNativePause)
        ImGui::SetCursorScreenPos(afterCanvas);
    else
        drawCanvas();
    screenMapInputBlocked = false;

    ImGui::Separator();
    const std::array<std::pair<CONTROLLERBUTTONS_T, const char*>, 8> controls = { {
        { BTN_CUSTOM_MAP_TRACKER_CONFIRM, "Select / Hint" },
        { BTN_CUSTOM_MAP_TRACKER_BACK, "Back" },
        { BTN_CUSTOM_MAP_TRACKER_FOCUS, "Focus Player" },
        { BTN_CUSTOM_MAP_TRACKER_OVERWORLD, "Overworld" },
        { BTN_CUSTOM_MAP_TRACKER_LIST_UP, "List Up" },
        { BTN_CUSTOM_MAP_TRACKER_LIST_DOWN, "List Down" },
        { BTN_CUSTOM_MAP_TRACKER_ZOOM_IN, "Zoom In" },
        { BTN_CUSTOM_MAP_TRACKER_ZOOM_OUT, "Zoom Out" },
    } };
    const auto deviceType =
        screenCursorUsesController ? Ship::PhysicalDeviceType::SDLGamepad : Ship::PhysicalDeviceType::Keyboard;
    std::array<std::string, controls.size()> controlLabels;
    for (size_t i = 0; i < controls.size(); ++i)
        controlLabels[i] = GetMapTrackerButtonLabel(controls[i].first, deviceType);
    const float footerFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
    if (renderingNativePause) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        auto measureFooter = [&]() {
            float width = -8.0f;
            for (size_t i = 0; i < controls.size(); ++i) {
                width += ImGui::CalcTextSize(controlLabels[i].c_str()).x + 10.0f + 4.0f +
                         ImGui::CalcTextSize(controls[i].second).x + 8.0f;
            }
            return width;
        };
        const float width = measureFooter();
        // Custom bindings can be longer than the defaults. Keep the native footer on one centered row.
        const float usableWidth = std::max(1.0f, availableWidth - 16.0f); // Allow for per-label pixel rounding.
        if (width > usableWidth) {
            const float fixedSpacing = controls.size() * 22.0f - 8.0f;
            const float scale =
                std::clamp((usableWidth - fixedSpacing) / std::max(1.0f, width - fixedSpacing), 0.35f, 1.0f);
            ImGui::SetWindowFontScale(footerFontScale * scale);
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availableWidth - measureFooter()) * 0.5f));
    }
    auto drawControlHint = [](CONTROLLERBUTTONS_T bitmask, const char* action, const std::string& key) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
        const ImU32 keyColor = renderingNativePause ? IM_COL32(83, 27, 22, 255) : IM_COL32(40, 46, 49, 255);
        ImGui::PushStyleColor(ImGuiCol_Button, keyColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, keyColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, keyColor);
        ImGui::PushID(static_cast<int>(bitmask));
        ImGui::SmallButton(key.c_str());
        ImGui::PopID();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("%s", action);
        ImGui::SameLine(0.0f, 8.0f);
    };
    for (size_t i = 0; i < controls.size(); ++i)
        drawControlHint(controls[i].first, controls[i].second, controlLabels[i]);
    if (renderingNativePause)
        ImGui::SetWindowFontScale(footerFontScale);

    if (!renderingNativePause) {
        ImDrawList* cursorDrawList = ImGui::GetForegroundDrawList();
        cursorDrawList->AddCircleFilled(screenCursorPosition, 16.0f, IM_COL32(236, 191, 95, 80), 24);
        cursorDrawList->AddCircle(screenCursorPosition, 16.0f, IM_COL32(255, 226, 152, 245), 24, 2.5f);
        cursorDrawList->AddCircleFilled(screenCursorPosition, 4.0f, IM_COL32(255, 255, 255, 245), 16);
    }

    screenConfirmPressed = false;
    screenBackPressed = false;
    screenOverworldPressed = false;
    screenFocusPressed = false;
    screenMousePressed = false;
    screenMapListNavigationDelta = 0;
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
                                   UIWidgets::FloatSliderOptions()
                                       .Format("%.1f")
                                       .Step(0.1f)
                                       .Min(0.3f)
                                       .Max(2.0f)
                                       .Color(THEME_COLOR)
                                       .DefaultValue(1.0f));
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
        if (UIWidgets::CVarCombobox(
                "In-Game Map Mode", CVAR_NATIVE_PAUSE, screenModes,
                UIWidgets::ComboboxOptions()
                    .Color(THEME_COLOR)
                    .DefaultIndex(DEFAULT_NATIVE_PAUSE_MODE)
                    .Tooltip("Fullscreen 2D: animated standalone panel. Native Pause 3D: "
                             "replace the game's Map page. Changes during a pause apply after closing it. "
                             "The classic ImGui tracker is unchanged."))) {
            SetScreenOpen(false, true);
            ResetScreenInput();
            if (gPlayState == nullptr || gPlayState->pauseCtx.state == 0)
                ResetNativePauseState();
            CheckTracker::InvalidateMapTrackerRenderCache();
        }
        UIWidgets::CVarCombobox("Window Type", CVAR_WINDOW_TYPE, windowTypes,
                                UIWidgets::ComboboxOptions().Color(THEME_COLOR).DefaultIndex(TRACKER_WINDOW_WINDOW));
        UIWidgets::CVarCheckbox(
            "Reveal Unspoiled Dungeon Checks", CVAR_MQ_SPOILERS,
            UIWidgets::CheckboxOptions()
                .Tooltip("Show dungeon checks before the dungeon has been spoiled, including MQ dungeons.")
                .Color(THEME_COLOR));
        UIWidgets::CVarCheckbox("Experimental Player Position", CVAR_PLAYER_POSITION,
                                UIWidgets::CheckboxOptions()
                                    .Color(THEME_COLOR)
                                    .Tooltip("Show position and facing using pack calibration points when available. "
                                             "Otherwise approximate from scene exits and map links."));

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void RegisterMapTrackerWidgets() {
    WidgetInfo showLogicWidget = { .name = "Show Logic", .type = WidgetType::WIDGET_CVAR_CHECKBOX };
    showLogicWidget.CVar(CVAR_SHOW_LOGIC)
        .Options(CheckboxOptions().Color(THEME_COLOR).Tooltip("Show check logic in Map Tracker tooltips."));
    SohGui::GetSohMenu()->AddSearchWidget({ showLogicWidget, "Randomizer", "Map Tracker", "General Settings" });
    WidgetInfo playerPositionWidget = { .name = "Experimental Player Position", .type = WidgetType::WIDGET_CVAR_CHECKBOX };
    playerPositionWidget.CVar(CVAR_PLAYER_POSITION)
        .Options(CheckboxOptions().Color(THEME_COLOR).Tooltip("Approximate player position and facing using scene exits and map links."));
    SohGui::GetSohMenu()->AddSearchWidget({ playerPositionWidget, "Randomizer", "Map Tracker", "Map Settings" });
}

static RegisterMenuInitFunc menuInitFunc(RegisterMapTrackerWidgets);
static RegisterShipInitFunc inputHookInitFunc([] {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) {
        ResetNativePauseState();
        SetScreenOpen(false, true);
        screenClosedForGameplay = false;
        suppressedGameplayButtons = 0;
    });
});
} // namespace MapTracker

extern "C" int MapTracker_IsGameplayPaused(void) {
    return CVarGetInteger(MapTracker::CVAR_SCREEN, 0) != 0;
}

extern "C" int MapTracker_UpdateGameplay(PlayState* play) {
    // Poll before vanilla Start or player actions can consume the same physical button.
    MapTracker::ToggleMapTrackerFromInput();
    if (MapTracker::NativePauseEnabled() && !MapTracker::screenClosedForGameplay && !MapTracker_IsGameplayPaused())
        return false;
    if (MapTracker_IsGameplayPaused() && !MapTracker::CanOpenScreen()) {
        MapTracker::SetScreenOpen(false, true);
    }
    if (MapTracker_IsGameplayPaused() || MapTracker::screenClosedForGameplay) {
        MapTracker::screenClosedForGameplay = false;
        // Discard menu input, including the closing frame, and quarantine held buttons until release.
        MapTracker::suppressedGameplayButtons = play->state.input[0].cur.button;
        for (Input& input : play->state.input) {
            input.cur = {};
            input.press = {};
            input.rel = {};
        }
        return true;
    }

    Input& input = play->state.input[0];
    const uint16_t suppressedButtons = MapTracker::suppressedGameplayButtons;
    MapTracker::suppressedGameplayButtons &= input.cur.button;
    input.cur.button &= ~MapTracker::suppressedGameplayButtons;
    input.press.button &= ~suppressedButtons;
    input.rel.button &= ~suppressedButtons;
    return false;
}
