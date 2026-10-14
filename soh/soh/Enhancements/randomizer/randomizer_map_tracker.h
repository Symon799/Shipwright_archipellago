#pragma once

#include "randomizerTypes.h"
#include "soh/cvar_prefixes.h"
#include <libultraship/libultraship.h>

namespace MapTracker {

inline constexpr const char* CVAR_DISPLAY_MODE = CVAR_TRACKER("MapTracker.DisplayMode");
inline constexpr const char* CVAR_SCREEN = CVAR_WINDOW("MapTrackerScreen");
inline constexpr const char* CVAR_FONT_SIZE = CVAR_TRACKER("MapTracker.FontSize");
inline constexpr const char* CVAR_DRAGGABLE = CVAR_TRACKER("MapTracker.Draggable");
inline constexpr const char* CVAR_DEBUG_WINDOW = CVAR_WINDOW("MapTrackerDebug");
inline constexpr const char* CVAR_SHOW_LOGIC = CVAR_TRACKER("MapTracker.ShowLogic");
inline constexpr const char* CVAR_SHOW_DEBUG_INFO = CVAR_TRACKER("MapTracker.ShowDebugInfo");
inline constexpr const char* CVAR_MQ_SPOILERS = CVAR_TRACKER("MapTracker.MQSpoilers");
inline constexpr const char* CVAR_PLAYER_POSITION = CVAR_TRACKER("MapTracker.ExperimentalPlayerPosition");
inline constexpr const char* CVAR_NATIVE_CUSTOM_BACKGROUND = CVAR_TRACKER("MapTracker.NativeCustomBackground");
enum class DisplayMode { Pause3D, Pause2D, Windowed, Floating };
inline constexpr int DEFAULT_DISPLAY_MODE = static_cast<int>(DisplayMode::Pause3D);
inline constexpr int DEFAULT_NATIVE_CUSTOM_BACKGROUND = 1;
DisplayMode GetDisplayMode();
bool IsInGameMode();
void Shutdown();

extern bool showLogicTooltip;
extern bool showMapDebugDetails;
extern bool screenConfirmPressed;
extern bool screenBackPressed;
extern bool screenMousePressed;
extern bool screenFocusPressed;
extern bool screenOverworldPressed;
extern bool screenZoomInPressed;
extern bool screenZoomOutPressed;
extern int screenMapListNavigationDelta;
extern bool screenCursorUsesController;
extern bool screenCursorNeedsCenter;
extern float screenCursorDeltaX;
extern float screenCursorDeltaY;
extern float screenPanDeltaX;
extern float screenPanDeltaY;
extern float screenZoomDelta;
extern ImVec2 screenCursorPosition;
extern bool renderingNativePause;
extern bool screenMapInputBlocked;
bool IsIntegratedMapView();
bool IsScreenCursorOverRect(const ImVec2& min, const ImVec2& max);

class MapTrackerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

class MapTrackerScreenWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;

  private:
    bool nativeMapListVisible = false;
    ImVec2 previousScreenCursorPosition = ImVec2(-1.0f, -1.0f);
};

class MapTrackerSettingsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

class MapTrackerDebugWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

} // namespace MapTracker
