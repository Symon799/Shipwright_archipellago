#pragma once

#ifdef _WIN32
#include <Windows.h>

static inline void Soh_DisableConsoleQuickEdit(void) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (input != NULL && input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode)) {
        // Selecting console text blocks log writes and can stall the game's main thread.
        SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    }
}
#endif
