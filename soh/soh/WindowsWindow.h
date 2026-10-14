#pragma once

#ifdef _WIN32
#include <Windows.h>

static inline bool Soh_DetachNativeWindowCallbacks(HWND window) {
    if (!IsWindow(window)) {
        return false;
    }

    // DXGI leaves its HWND alive after Context teardown. Late focus/destruction events
    // must not call the backend, which dereferences Context and the ImGui instance.
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return true;
}
#endif
