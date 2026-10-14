#pragma once

#include <algorithm>

namespace MapTracker {
enum class DisplayMode { Pause3D, Pause2D, Windowed, Floating };
inline constexpr int DEFAULT_DISPLAY_MODE = static_cast<int>(DisplayMode::Pause3D);

inline DisplayMode ResolveDisplayMode(int mode, bool legacyInGame, bool legacyNative, bool legacyFloating) {
    if (mode >= 0) {
        return static_cast<DisplayMode>(std::clamp(mode, 0, 3));
    }
    if (legacyInGame) {
        return legacyNative ? DisplayMode::Pause3D : DisplayMode::Pause2D;
    }
    return legacyFloating ? DisplayMode::Floating : DisplayMode::Windowed;
}
} // namespace MapTracker
