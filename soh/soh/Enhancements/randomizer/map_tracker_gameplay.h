#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

int MapTracker_IsGameplayPaused(void);
int MapTracker_UpdateGameplay(PlayState* play);

#ifdef __cplusplus
}
#endif
