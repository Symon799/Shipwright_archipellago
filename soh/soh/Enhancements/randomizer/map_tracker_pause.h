#pragma once

#ifdef __cplusplus
#include <memory>
#include <imgui.h>
struct ImGuiWindow;
namespace Ship {
class GuiTexture;
}
namespace MapTracker {
bool NativePauseEnabled();
bool NativePauseInteractive();
bool NativePauseOpening();
bool NativePauseNeedsPreview();
void ApplyNativePauseFocus();
bool GetNativePauseBounds(ImVec2& position, ImVec2& size);
void CaptureNativePausePage(ImGuiWindow* root);
void RegisterNativePauseTexture(ImTextureID id, const std::shared_ptr<Ship::GuiTexture>& texture);
ImTextureID GetNativePauseBackgroundTexture();
void DrawNativePauseBackground(ImTextureID texture);
void ClearNativePauseTextures();
void ShutdownNativePauseRenderer();
void ResetNativePauseState();
void RequestNativePauseMap(bool controller);
void RequestNativePauseClose();
void SetNativePauseShoulderInput(bool rightShoulder);
} // namespace MapTracker
extern "C" {
#endif
struct PlayState;
int MapTracker_UseNativePausePage(void);
void MapTracker_BeginPauseSession(struct PlayState* play, int trackerRequested);
void MapTracker_EndPauseSession(struct PlayState* play);
void* MapTracker_GetPauseTitleTexture(void);
void MapTracker_PrepareNativePausePage(void);
void MapTracker_PositionNativePauseButtons(struct PlayState* play);
int MapTracker_ConsumePauseMapRequest(void);
int MapTracker_HandleNativePauseInput(struct PlayState* play);
void MapTracker_DrawNativePausePage(struct PlayState* play);
#ifdef __cplusplus
}
#endif
