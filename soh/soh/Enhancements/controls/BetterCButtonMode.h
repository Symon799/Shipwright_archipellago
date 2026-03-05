#ifndef BETTER_C_BUTTON_MODE_H
#define BETTER_C_BUTTON_MODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Applies the optional right-stick single C-button mode to a button mask.
// If the mode is disabled, returns baseButtons unchanged.
uint16_t BetterCButtonMode_ApplySingleDirectionWithDeadzoneToCButtons(uint8_t portIndex, uint16_t baseButtons,
                                                                       int8_t rightStickX, int8_t rightStickY);

#ifdef __cplusplus
}
#endif

#endif // BETTER_C_BUTTON_MODE_H
