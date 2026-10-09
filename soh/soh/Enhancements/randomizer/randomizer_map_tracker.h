#pragma once

#include "randomizerTypes.h"
#include "soh/cvar_prefixes.h"
#include <libultraship/libultraship.h>

namespace MapTracker {

inline constexpr const char* CVAR_WINDOW_TYPE = CVAR_TRACKER("MapTracker.WindowType");
inline constexpr const char* CVAR_FONT_SIZE = CVAR_TRACKER("MapTracker.FontSize");
inline constexpr const char* CVAR_DRAGGABLE = CVAR_TRACKER("MapTracker.Draggable");
inline constexpr const char* CVAR_SHOW_ONLY_PAUSED = CVAR_TRACKER("MapTracker.ShowOnlyPaused");
inline constexpr const char* CVAR_SHOW_LOGIC = CVAR_TRACKER("MapTracker.ShowLogic");
inline constexpr const char* CVAR_SHOW_TOTALS = CVAR_TRACKER("MapTracker.ShowTotals");
inline constexpr const char* CVAR_SHOW_DEBUG_INFO = CVAR_TRACKER("MapTracker.ShowDebugInfo");
inline constexpr const char* CVAR_MQ_SPOILERS = CVAR_TRACKER("MapTracker.MQSpoilers");

extern bool showLogicTooltip;
extern bool showMapDebugDetails;

class MapTrackerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

class MapTrackerSettingsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

} // namespace MapTracker
