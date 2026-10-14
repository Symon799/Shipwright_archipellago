#pragma once

#include <imgui.h>

namespace MapTracker {
// A bounds-only proxy. It must never acquire the real viewport's backend ownership.
class MapInteractionViewport {
  public:
    const ImGuiViewport* Update(const ImGuiViewport& source, ImVec2 position, ImVec2 size) {
        viewport.ID = source.ID;
        viewport.Flags = source.Flags;
        viewport.DpiScale = source.DpiScale;
        viewport.Pos = viewport.WorkPos = position;
        viewport.Size = viewport.WorkSize = size;
        return &viewport;
    }

  private:
    ImGuiViewport viewport;
};
} // namespace MapTracker
