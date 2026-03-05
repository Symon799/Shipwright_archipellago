#include "BetterCButtonMode.h"

#include <cmath>

#include <SDL2/SDL.h>

#include <libultraship/bridge.h>
#include "libultraship/libultra/controller.h"

#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>

#include "soh/cvar_prefixes.h"

namespace {
// libultraship stick processing uses the N64 virtual stick range [-85, 85].
constexpr int32_t kBetterCButtonModeVirtualStickMax = 85;
constexpr int32_t kBetterCButtonModeActivationThreshold = 8;

// Better C-Buttons deadzone percentage.
// Change this (e.g. to 60) if you want a different deadzone.
constexpr int32_t kBetterCButtonModeDeadzonePercent = 80;
static_assert(kBetterCButtonModeDeadzonePercent >= 0 && kBetterCButtonModeDeadzonePercent <= 100,
              "Better C-Buttons deadzone percent must be between 0 and 100.");

constexpr int32_t kBetterCButtonModeDeadzone =
    (kBetterCButtonModeVirtualStickMax * kBetterCButtonModeDeadzonePercent) / 100;
} // namespace

static int32_t GetRawRightStickResolved(uint8_t portIndex) {
    const auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetControlDeck() == nullptr) {
        return -1;
    }

    const auto connectedDeviceManager = context->GetControlDeck()->GetConnectedPhysicalDeviceManager();
    if (connectedDeviceManager == nullptr) {
        return -1;
    }

    const auto gamepads = connectedDeviceManager->GetConnectedSDLGamepadsForPort(portIndex);
    if (gamepads.empty()) {
        return -1;
    }

    int32_t bestX = 0;
    int32_t bestY = 0;
    int32_t bestMagnitudeSquared = -1;

    for (const auto& [instanceId, gamepad] : gamepads) {
        (void)instanceId;
        const int32_t rawX = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTX);
        const int32_t rawY = SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTY);

        const int32_t stickX =
            static_cast<int32_t>(std::lround((rawX / static_cast<double>(SDL_JOYSTICK_AXIS_MAX)) *
                                             kBetterCButtonModeVirtualStickMax));
        const int32_t stickY = static_cast<int32_t>(
            std::lround((-rawY / static_cast<double>(SDL_JOYSTICK_AXIS_MAX)) * kBetterCButtonModeVirtualStickMax));

        const int32_t magnitudeSquared = stickX * stickX + stickY * stickY;
        if (magnitudeSquared > bestMagnitudeSquared) {
            bestMagnitudeSquared = magnitudeSquared;
            bestX = stickX;
            bestY = stickY;
        }
    }

    if (bestMagnitudeSquared < kBetterCButtonModeActivationThreshold * kBetterCButtonModeActivationThreshold) {
        return -1;
    }

    if (bestMagnitudeSquared < kBetterCButtonModeDeadzone * kBetterCButtonModeDeadzone) {
        return 0;
    }

    const int32_t absX = std::abs(bestX);
    const int32_t absY = std::abs(bestY);

    if (absX >= absY) {
        return (bestX >= 0) ? BTN_CRIGHT : BTN_CLEFT;
    }

    return (bestY >= 0) ? BTN_CUP : BTN_CDOWN;
}

extern "C" {
uint16_t BetterCButtonMode_ApplySingleDirectionWithDeadzoneToCButtons(uint8_t portIndex, uint16_t baseButtons,
                                                                       int8_t rightStickX, int8_t rightStickY) {
    if (!CVarGetInteger(CVAR_SETTING("Controls.RightStickSingleCButtonMode"), 0)) {
        return baseButtons;
    }

    const uint16_t cButtonsMask = BTN_CUP | BTN_CDOWN | BTN_CLEFT | BTN_CRIGHT;
    const int32_t rawSingleCButton = GetRawRightStickResolved(portIndex);

    if (rawSingleCButton >= 0) {
        uint16_t filteredButtons = baseButtons & ~cButtonsMask;
        if (rawSingleCButton > 0) {
            filteredButtons |= static_cast<uint16_t>(rawSingleCButton);
        }
        return filteredButtons;
    }

    if (rightStickX == 0 && rightStickY == 0) {
        return baseButtons;
    }

    const int32_t x = rightStickX;
    const int32_t y = rightStickY;
    const int32_t magnitudeSquared = x * x + y * y;

    uint16_t filteredButtons = baseButtons & ~cButtonsMask;
    if (magnitudeSquared < kBetterCButtonModeDeadzone * kBetterCButtonModeDeadzone) {
        return filteredButtons;
    }

    const int32_t absX = std::abs(x);
    const int32_t absY = std::abs(y);
    filteredButtons |= (absX >= absY) ? (x >= 0 ? BTN_CRIGHT : BTN_CLEFT) : (y >= 0 ? BTN_CUP : BTN_CDOWN);

    return filteredButtons;
}
} // extern "C"
