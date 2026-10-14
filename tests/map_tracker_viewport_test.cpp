#include "check.h"
#include "soh/soh/Enhancements/randomizer/map_tracker_viewport.h"

int main() {
    MapTracker::MapInteractionViewport proxy;
    ImGuiViewport source;
    source.ID = 17;
    source.DpiScale = 1.5f;
    source.PlatformUserData = reinterpret_cast<void*>(1);
    source.RendererUserData = reinterpret_cast<void*>(2);
    source.PlatformHandle = reinterpret_cast<void*>(3);
    source.PlatformHandleRaw = reinterpret_cast<void*>(4);
    source.PlatformWindowCreated = true;
    const auto* result = proxy.Update(source, { 100, 200 }, { 640, 480 });
    CHECK(result != &source && result->ID == 17 && result->DpiScale == 1.5f);
    CHECK(result->Pos.x == 100 && result->WorkPos.y == 200 && result->Size.x == 640 && result->WorkSize.y == 480);
    CHECK(!result->PlatformUserData && !result->RendererUserData && !result->PlatformHandle &&
          !result->PlatformHandleRaw && !result->PlatformWindowCreated);
    CHECK(source.PlatformUserData && source.RendererUserData);
    CHECK(proxy.Update(source, { 10, 20 }, { 320, 240 }) == result);
    CHECK(result->Pos.x == 10 && result->Size.x == 320);
    // The real backend owns source's data. Emulate its cleanup; proxy destruction
    // must be independent.
    source.PlatformUserData = source.RendererUserData = nullptr;
    source.PlatformHandle = source.PlatformHandleRaw = nullptr;
    source.PlatformWindowCreated = false;
}
