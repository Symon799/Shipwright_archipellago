#include "check.h"
#include "soh/soh/WindowsWindow.h"

static bool contextAlive = true;
static int callbacks = 0;

static LRESULT CALLBACK BackendProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    CHECK(contextAlive);
    ++callbacks;
    return DefWindowProcW(window, message, wParam, lParam);
}

int main() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = BackendProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"SoHValidationShutdown";
    CHECK(RegisterClassW(&windowClass));
    CHECK(!Soh_DetachNativeWindowCallbacks(nullptr));
    for (int i = 0; i < 3; ++i) {
        contextAlive = true;
        HWND window = CreateWindowW(windowClass.lpszClassName, L"Validation", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                                    nullptr, nullptr, windowClass.hInstance, nullptr);
        CHECK(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, 123);
        CHECK(Soh_DetachNativeWindowCallbacks(window));
        CHECK(Soh_DetachNativeWindowCallbacks(window));
        CHECK(GetWindowLongPtrW(window, GWLP_USERDATA) == 0);
        const int previousCallbacks = callbacks;
        contextAlive = false;
        SendMessageW(window, WM_KILLFOCUS, 0, 0);
        SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
        SendMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(100, 100));
        CHECK(DestroyWindow(window));
        CHECK(callbacks == previousCallbacks);
        CHECK(!Soh_DetachNativeWindowCallbacks(window));
    }
    CHECK(UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance));
}
