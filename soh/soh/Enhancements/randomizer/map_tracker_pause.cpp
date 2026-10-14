#include "map_tracker_pause.h"
#include "map_tracker_internal.h"
#include "randomizer_map_tracker.h"
#include "soh/ShipUtils.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include <imgui_internal.h>
#include <ship/window/gui/resource/GuiTexture.h>
#include <spdlog/spdlog.h>
#include <fast/Fast3dWindow.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <deque>
#include <optional>
#include <unordered_map>

extern "C" {
#include "global.h"
extern PlayState* gPlayState;
void KaleidoScope_SwitchPage(PauseContext* pauseCtx, u8 direction);
}

namespace MapTracker {
namespace {
// Large LoadTile uploads are supported by Fast3D. A one-pixel gutter preserves filtering at seams.
constexpr int TILE_SIZE = 254;
constexpr int TILE_TEXTURE_SIZE = TILE_SIZE + 2;
constexpr float VERTEX_SCALE = 16.0f;
struct Tile {
    std::array<uint8_t, TILE_TEXTURE_SIZE * TILE_TEXTURE_SIZE * 4> pixels{};
    std::weak_ptr<Fast::Interpreter> interpreter;
    ~Tile() {
        if (auto renderer = interpreter.lock())
            renderer->TextureCacheDelete(pixels.data());
    }
};
struct CpuTexture {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    std::shared_ptr<Ship::GuiTexture> source;
    std::weak_ptr<Fast::Interpreter> interpreter;
    std::unordered_map<int, std::shared_ptr<Tile>> tiles;
    std::shared_ptr<Tile> GetTile(int x, int y) {
        const int key = y * ((width + TILE_SIZE - 1) / TILE_SIZE) + x;
        if (auto found = tiles.find(key); found != tiles.end())
            return found->second;
        auto tile = std::make_shared<Tile>();
        tile->interpreter = interpreter;
        const uint8_t* data = source ? source->Data : pixels.data();
        const int startX = x * TILE_SIZE - 1;
        const int firstColumn = std::max(0, -startX);
        const int endColumn = std::min(TILE_TEXTURE_SIZE, width - startX);
        for (int row = 0; row < TILE_TEXTURE_SIZE; ++row) {
            const int sourceY = std::clamp(y * TILE_SIZE + row - 1, 0, height - 1);
            auto* destination = tile->pixels.data() + row * TILE_TEXTURE_SIZE * 4;
            const auto* sourceRow = data + sourceY * width * 4;
            for (int col = 0; col < firstColumn; ++col)
                std::memcpy(destination + col * 4, sourceRow, 4);
            std::memcpy(destination + firstColumn * 4, sourceRow + (startX + firstColumn) * 4,
                        (endColumn - firstColumn) * 4);
            for (int col = endColumn; col < TILE_TEXTURE_SIZE; ++col)
                std::memcpy(destination + col * 4, sourceRow + (width - 1) * 4, 4);
        }
        tiles.emplace(key, tile);
        return tile;
    }
};
struct Vertex {
    ImVec2 position, uv;
    ImVec4 color;
};
struct Triangle {
    std::array<Vertex, 3> vertices;
    std::shared_ptr<Tile> tile;
};
struct PageFrame {
    std::vector<Triangle> triangles;
    ImVec2 origin, size;
};
struct PageSubmission {
    std::shared_ptr<const PageFrame> geometry;
    std::vector<Vtx> vertices;
    std::vector<Gfx> commands;
};
struct PagePlane {
    ImVec2 minimum, maximum;
};
PagePlane GetPagePlane(const Vtx* vertices) {
    PagePlane plane{ ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX) };
    // The first 15 quads are the vanilla page background, before its map-specific content.
    for (int i = 0; i < 60; ++i) {
        plane.minimum.x = std::min(plane.minimum.x, static_cast<float>(vertices[i].v.ob[0]));
        plane.minimum.y = std::min(plane.minimum.y, static_cast<float>(vertices[i].v.ob[1]));
        plane.maximum.x = std::max(plane.maximum.x, static_cast<float>(vertices[i].v.ob[0]));
        plane.maximum.y = std::max(plane.maximum.y, static_cast<float>(vertices[i].v.ob[1]));
    }
    return plane;
}

std::optional<ImVec2> ProjectPausePoint(Vec3f local, const float view[4][4], const float projection[4][4],
                                        const Fast::Interpreter& interpreter) {
    Vec3f world;
    Matrix_MultVec3f(&local, &world);
    std::array<float, 4> camera{}, clip{};
    const std::array<float, 4> vector{ world.x, world.y, world.z, 1.0f };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            camera[i] += vector[j] * view[j][i];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            clip[i] += camera[j] * projection[j][i];
    if (clip[3] <= 0.0f)
        return std::nullopt;
    const auto& viewport = interpreter.mGameWindowViewport;
    return ImVec2(viewport.x + (interpreter.AdjXForAspectRatio(clip[0]) / clip[3] + 1.0f) * viewport.width * 0.5f,
                  viewport.y + (1.0f - clip[1] / clip[3]) * viewport.height * 0.5f) +
           ImGui::GetMainViewport()->Pos;
}

void AlignNativePauseButton(Vtx* vertices, float leftScreen, float rightScreen, float targetEdge, bool leftButton) {
    if (rightScreen <= leftScreen)
        return;
    const float localWidth = vertices[1].v.ob[0] - vertices[0].v.ob[0];
    const int shift =
        std::lround((targetEdge - (leftButton ? rightScreen : leftScreen)) * localWidth / (rightScreen - leftScreen));
    for (int i = 0; i < 4; ++i)
        vertices[i].v.ob[0] += shift;
}
std::unordered_map<ImTextureID, std::shared_ptr<CpuTexture>> textures;
std::shared_ptr<Ship::GuiTexture> nativePauseBackgroundTexture;
std::shared_ptr<PageFrame> capturedPage;
// Display lists can be replayed for interpolated frames; never retire their data on a GUI update.
std::deque<std::shared_ptr<PageSubmission>> submittedPages;
ImVec2 pagePosition, pageSize;
bool boundsReady = false;
bool openRequested = false, closeRequested = false, rightShoulderPressed = false;
bool previewRequested = false;
bool focusRequested = false;
bool pauseSessionActive = false, nativePauseSession = false;
bool restorePausePage = false;
u16 previousPausePage = PAUSE_ITEM;

void UpdateNativePauseLayoutBounds(const PauseContext& pause, const PagePlane& plane,
                                   const Fast::Interpreter& interpreter) {
    // Lay out at the final Map-page pose from the first frame. Only the actual page geometry animates.
    Mtx settledView;
    float view[4][4], projection[4][4];
    guLookAt(&settledView, -64.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    guMtxL2F(view, &settledView);
    guMtxL2F(projection, pause.view.projectionPtr);
    Matrix_Push();
    Matrix_Translate(static_cast<float>(WREG(3)) / 100.0f, 0.0f, 0.0f, MTXMODE_NEW);
    Matrix_Scale(0.78f, 0.78f, 0.78f, MTXMODE_APPLY);
    Matrix_RotateY(-1.57f, MTXMODE_APPLY);
    ImVec2 minimum(FLT_MAX, FLT_MAX), maximum(-FLT_MAX, -FLT_MAX);
    for (const auto point : { plane.minimum, ImVec2(plane.minimum.x, plane.maximum.y), plane.maximum,
                              ImVec2(plane.maximum.x, plane.minimum.y) }) {
        const auto projected =
            ProjectPausePoint(Vec3f{ point.x, point.y - pause.offsetY, 0.0f }, view, projection, interpreter);
        if (!projected)
            continue;
        minimum.x = std::min(minimum.x, projected->x);
        minimum.y = std::min(minimum.y, projected->y);
        maximum.x = std::max(maximum.x, projected->x);
        maximum.y = std::max(maximum.y, projected->y);
    }
    Matrix_Pop();
    if (maximum.x > minimum.x && maximum.y > minimum.y) {
        pagePosition = minimum;
        pageSize = maximum - minimum;
        boundsReady = true;
    }
}

Vertex Interpolate(const Vertex& a, const Vertex& b, float t) {
    return { a.position + (b.position - a.position) * t, a.uv + (b.uv - a.uv) * t,
             ImVec4(a.color.x + (b.color.x - a.color.x) * t, a.color.y + (b.color.y - a.color.y) * t,
                    a.color.z + (b.color.z - a.color.z) * t, a.color.w + (b.color.w - a.color.w) * t) };
}

// Clip in page space before projection, and in texture space before splitting large images into RDP tiles.
template <typename Coordinate>
std::vector<Vertex> Clip(const std::vector<Vertex>& polygon, Coordinate coordinate, float edge, bool minimum) {
    std::vector<Vertex> result;
    if (polygon.empty())
        return result;
    result.reserve(polygon.size() + 1);
    Vertex previous = polygon.back();
    float previousDistance = (coordinate(previous) - edge) * (minimum ? 1.0f : -1.0f);
    for (const Vertex& vertex : polygon) {
        const float distance = (coordinate(vertex) - edge) * (minimum ? 1.0f : -1.0f);
        if ((distance >= 0.0f) != (previousDistance >= 0.0f)) {
            result.push_back(Interpolate(previous, vertex, previousDistance / (previousDistance - distance)));
        }
        if (distance >= 0.0f)
            result.push_back(vertex);
        previous = vertex;
        previousDistance = distance;
    }
    return result;
}

void AppendPolygon(PageFrame& frame, const std::vector<Vertex>& polygon, const std::shared_ptr<Tile>& tile, int tileX,
                   int tileY) {
    const ImVec2 offset(tileX * TILE_SIZE - 1, tileY * TILE_SIZE - 1);
    for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        Triangle triangle{ { polygon[0], polygon[i], polygon[i + 1] }, tile };
        for (auto& vertex : triangle.vertices)
            vertex.uv -= offset;
        frame.triangles.push_back(std::move(triangle));
    }
}

void AppendDrawList(PageFrame& frame, const ImDrawList& list) {
    frame.triangles.reserve(frame.triangles.size() + list.IdxBuffer.Size / 3);
    std::vector<Vertex> polygon;
    polygon.reserve(8);
    for (const auto& command : list.CmdBuffer) {
        if (command.UserCallback != nullptr || command.ElemCount == 0)
            continue;
        const auto found = textures.find(command.GetTexID());
        if (found == textures.end())
            continue;
        CpuTexture& texture = *found->second;
        for (unsigned int index = 0; index + 2 < command.ElemCount; index += 3) {
            polygon.clear();
            ImVec2 positionMin(FLT_MAX, FLT_MAX), positionMax(-FLT_MAX, -FLT_MAX);
            for (unsigned int i = 0; i < 3; ++i) {
                const auto& source = list.VtxBuffer[list.IdxBuffer[command.IdxOffset + index + i] + command.VtxOffset];
                polygon.push_back({ source.pos, ImVec2(source.uv.x * texture.width, source.uv.y * texture.height),
                                    ImGui::ColorConvertU32ToFloat4(source.col) });
                positionMin.x = std::min(positionMin.x, source.pos.x);
                positionMin.y = std::min(positionMin.y, source.pos.y);
                positionMax.x = std::max(positionMax.x, source.pos.x);
                positionMax.y = std::max(positionMax.y, source.pos.y);
            }
            if (positionMax.x <= command.ClipRect.x || positionMax.y <= command.ClipRect.y ||
                positionMin.x >= command.ClipRect.z || positionMin.y >= command.ClipRect.w)
                continue;
            if (positionMin.x < command.ClipRect.x)
                polygon = Clip(polygon, [](const Vertex& v) { return v.position.x; }, command.ClipRect.x, true);
            if (positionMin.y < command.ClipRect.y)
                polygon = Clip(polygon, [](const Vertex& v) { return v.position.y; }, command.ClipRect.y, true);
            if (positionMax.x > command.ClipRect.z)
                polygon = Clip(polygon, [](const Vertex& v) { return v.position.x; }, command.ClipRect.z, false);
            if (positionMax.y > command.ClipRect.w)
                polygon = Clip(polygon, [](const Vertex& v) { return v.position.y; }, command.ClipRect.w, false);
            if (polygon.size() < 3)
                continue;
            ImVec2 minimum(texture.width, texture.height), maximum(0.0f, 0.0f);
            for (const auto& vertex : polygon) {
                minimum.x = std::min(minimum.x, vertex.uv.x);
                minimum.y = std::min(minimum.y, vertex.uv.y);
                maximum.x = std::max(maximum.x, vertex.uv.x);
                maximum.y = std::max(maximum.y, vertex.uv.y);
            }
            const int firstX = std::clamp(static_cast<int>(minimum.x) / TILE_SIZE, 0, (texture.width - 1) / TILE_SIZE);
            const int firstY = std::clamp(static_cast<int>(minimum.y) / TILE_SIZE, 0, (texture.height - 1) / TILE_SIZE);
            const int lastX =
                std::clamp(static_cast<int>(maximum.x) / TILE_SIZE, firstX, (texture.width - 1) / TILE_SIZE);
            const int lastY =
                std::clamp(static_cast<int>(maximum.y) / TILE_SIZE, firstY, (texture.height - 1) / TILE_SIZE);
            // Text and marker triangles normally fit one tile: no texture-space clipping is needed.
            if (firstX == lastX && firstY == lastY) {
                AppendPolygon(frame, polygon, texture.GetTile(firstX, firstY), firstX, firstY);
                continue;
            }
            for (int y = firstY; y <= lastY; ++y) {
                for (int x = firstX; x <= lastX; ++x) {
                    auto tilePolygon = Clip(polygon, [](const Vertex& v) { return v.uv.x; }, x * TILE_SIZE, true);
                    tilePolygon = Clip(tilePolygon, [](const Vertex& v) { return v.uv.y; }, y * TILE_SIZE, true);
                    tilePolygon = Clip(tilePolygon, [](const Vertex& v) { return v.uv.x; }, (x + 1) * TILE_SIZE, false);
                    tilePolygon = Clip(tilePolygon, [](const Vertex& v) { return v.uv.y; }, (y + 1) * TILE_SIZE, false);
                    if (tilePolygon.size() < 3)
                        continue;
                    AppendPolygon(frame, tilePolygon, texture.GetTile(x, y), x, y);
                }
            }
        }
    }
}

void AppendNativePauseWindow(PageFrame& frame, ImGuiWindow* window) {
    if (!window->Active)
        return;
    AppendDrawList(frame, *window->DrawList);
    window->DrawList->CmdBuffer.clear();
    // The global window list is sorted at EndFrame. New map children need this frame's painter order.
    for (ImGuiWindow* child : window->DC.ChildWindows)
        AppendNativePauseWindow(frame, child);
}

void RegisterPixels(ImTextureID id, const uint8_t* pixels, int width, int height,
                    const std::shared_ptr<Ship::GuiTexture>& source = nullptr) {
    if (!id || !pixels || width <= 0 || height <= 0)
        return;
    auto texture = std::make_shared<CpuTexture>();
    const auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (window)
        texture->interpreter = window->GetInterpreterWeak();
    texture->width = width;
    texture->height = height;
    texture->source = source;
    if (!source)
        texture->pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    textures[id] = std::move(texture);
}
} // namespace

bool NativePauseEnabled() {
    // Keep the renderer and input owner stable until the current vanilla pause has closed.
    const bool enabled =
        pauseSessionActive ? nativePauseSession
                           : GetDisplayMode() == DisplayMode::Pause3D;
    return enabled && GameInteractor::IsSaveLoaded() && CheckTracker::initialized;
}
bool NativePauseInteractive() {
    return NativePauseEnabled() && gPlayState != nullptr && gPlayState->pauseCtx.state == 6 &&
           gPlayState->pauseCtx.pageIndex == PAUSE_MAP && gPlayState->pauseCtx.unk_1E4 == 0 &&
           gPlayState->pauseCtx.debugState == 0;
}
bool NativePauseOpening() {
    return NativePauseEnabled() && gPlayState != nullptr && gPlayState->pauseCtx.state >= 1 &&
           gPlayState->pauseCtx.state < 6 && gPlayState->pauseCtx.debugState == 0;
}
bool NativePauseNeedsPreview() {
    return NativePauseOpening() ||
           (previewRequested && NativePauseEnabled() && gPlayState != nullptr && gPlayState->pauseCtx.state >= 1 &&
            gPlayState->pauseCtx.state <= 6 && gPlayState->pauseCtx.debugState == 0);
}
void ApplyNativePauseFocus() {
    if (focusRequested && CheckTracker::mapTrackerState.loaded) {
        CheckTracker::FocusMapTrackerOnPlayer();
        focusRequested = false;
    }
}
void PrepareNativePausePage() {
    focusRequested = true;
    ApplyNativePauseFocus();
    capturedPage.reset();
    previewRequested = true;
}
bool GetNativePauseBounds(ImVec2& position, ImVec2& size) {
    if (!boundsReady)
        return false;
    position = pagePosition;
    size = pageSize;
    return true;
}
void RegisterNativePauseTexture(ImTextureID id, const std::shared_ptr<Ship::GuiTexture>& texture) {
    RegisterPixels(id, texture->Data, texture->Metadata.Width, texture->Metadata.Height, texture);
}
ImTextureID GetNativePauseBackgroundTexture() {
    static bool attempted = false;
    auto& texture = nativePauseBackgroundTexture;
    static ImTextureID textureId = 0;
    const auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (!gui)
        return 0;
    if (!attempted) {
        attempted = true;
        try {
            auto init = std::make_shared<Ship::ResourceInitData>();
            init->Format = RESOURCE_FORMAT_BINARY;
            init->Type = RESOURCE_TYPE_GUI_TEXTURE;
            init->ResourceVersion = 0;
            init->Path = "textures/map_tracker/pause_map_tracker.png";
            texture = std::dynamic_pointer_cast<Ship::GuiTexture>(
                Ship::Context::GetInstance()->GetResourceManager()->LoadResource(init->Path, false, init));
            if (texture && texture->Data && texture->Metadata.Width > 0 && texture->Metadata.Height > 0) {
                constexpr const char* name = "MapTracker.NativePauseBackground";
                gui->LoadTextureFromResource(name, texture);
                textureId = gui->GetTextureByName(name);
            }
        } catch (const std::exception& error) {
            SPDLOG_WARN("Could not load Map Tracker background: {}", error.what());
        }
    }
    // Pack reloads clear native registrations; retain the decoded background and upload only once.
    if (textureId && !textures.contains(textureId))
        RegisterNativePauseTexture(textureId, texture);
    return textureId;
}
void DrawNativePauseBackground(ImTextureID texture) {
    if (!texture)
        return;
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 maximum = position + ImGui::GetWindowSize();
    auto* draw = ImGui::GetWindowDrawList();
    // Draw beneath all content, including padding. Only reserve an inset when the window has a border.
    const ImVec2 inset(ImGui::GetStyle().WindowBorderSize, ImGui::GetStyle().WindowBorderSize);
    draw->PushClipRect(position, maximum, false);
    draw->AddImage(texture, position + inset, maximum - inset);
    draw->PopClipRect();
}
void ClearNativePauseTextures() {
    textures.clear();
    capturedPage.reset();
    boundsReady = false;
}
void ShutdownNativePauseRenderer() {
    // Display-list snapshots can release renderer-owned tiles; the renderer must still be alive.
    submittedPages.clear();
    ClearNativePauseTextures();
    nativePauseBackgroundTexture.reset();
    ResetNativePauseState();
}
void ResetNativePauseState() {
    pauseSessionActive = nativePauseSession = restorePausePage = false;
    openRequested = closeRequested = rightShoulderPressed = false;
    previewRequested = focusRequested = false;
    // Texture registrations follow the pack lifecycle, not switching between already-loaded views.
    capturedPage.reset();
    boundsReady = false;
}
void RequestNativePauseMap(bool controller) {
    openRequested = true;
    screenCursorUsesController = controller;
    screenCursorNeedsCenter = true;
    PrepareNativePausePage();
}
void RequestNativePauseClose() {
    closeRequested = true;
}
void SetNativePauseShoulderInput(bool rightShoulder) {
    rightShoulderPressed |= rightShoulder;
}

void DrawNativePauseCursor() {
    if (!NativePauseInteractive())
        return;
    // Always above 2D detail windows, even when only the outer ring overlaps a popup.
    auto* foreground = ImGui::GetForegroundDrawList();
    foreground->AddCircle(screenCursorPosition, 16.0f, IM_COL32(255, 236, 161, 255), 24, 2.5f);
    foreground->AddCircleFilled(screenCursorPosition, 4.0f, IM_COL32_WHITE, 12);
}

void CaptureNativePausePage(ImGuiWindow* root) {
    const auto* atlas = ImGui::GetIO().Fonts;
    if (!textures.contains(atlas->TexID)) {
        unsigned char* pixels;
        int width, height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        RegisterPixels(atlas->TexID, pixels, width, height);
    }
    auto frame = std::make_shared<PageFrame>();
    frame->origin = root->Pos;
    frame->size = root->Size;
    AppendNativePauseWindow(*frame, root);
    DrawNativePauseCursor();
    capturedPage = std::move(frame);
    previewRequested = false;
}

static uint8_t GetPauseTitleCharacter(unsigned int codepoint) {
    if (codepoint >= 0x20 && codepoint <= 0x7E)
        return static_cast<uint8_t>(codepoint);
    constexpr std::array<unsigned int, 31> accentedCharacters = {
        0xC0, 0xCE, 0xC2, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCF, 0xD4, 0xD6, 0xD9, 0xDB, 0xDC, 0xDF,
        0xE0, 0xE1, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEF, 0xF4, 0xF6, 0xF9, 0xFB, 0xFC,
    };
    const auto found = std::find(accentedCharacters.begin(), accentedCharacters.end(), codepoint);
    return found == accentedCharacters.end() ? '?' : static_cast<uint8_t>(0x80 + (found - accentedCharacters.begin()));
}

static std::array<uint8_t, 128 * 16 / 2> BuildPauseTitleTexture(const std::string& title) {
    struct Glyph {
        const uint8_t* pixels;
        int x;
    };
    std::vector<Glyph> glyphs;
    int advance = 0;
    const char* character = title.c_str();
    const char* end = character + title.size();
    // Bound temporary raster size even if a resource pack provides an unusually long name.
    while (character < end && glyphs.size() < 256) {
        unsigned int codepoint;
        const int bytes = ImTextCharFromUtf8(&codepoint, character, end);
        if (bytes <= 0)
            break;
        character += bytes;
        const uint8_t gameCharacter = GetPauseTitleCharacter(codepoint);
        const char* texture = static_cast<const char*>(Ship_GetCharFontTexture(gameCharacter));
        glyphs.push_back({ static_cast<const uint8_t*>(ResourceGetDataByName(texture)), advance });
        advance += std::max(1, static_cast<int>(std::lround(Ship_GetCharFontWidth(gameCharacter))));
    }
    const int sourceWidth = advance + 16;
    std::vector<uint8_t> source(sourceWidth * 16, 0);
    int left = sourceWidth, right = -1, top = 16, bottom = -1;
    for (const auto& glyph : glyphs) {
        if (!glyph.pixels)
            continue;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const int index = y * 16 + x;
                const uint8_t intensity = (glyph.pixels[index / 2] >> (index % 2 ? 0 : 4)) & 15;
                if (!intensity)
                    continue;
                auto& pixel = source[y * sourceWidth + glyph.x + x];
                pixel = std::max(pixel, intensity);
                left = std::min(left, glyph.x + x);
                right = std::max(right, glyph.x + x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    std::array<uint8_t, 128 * 16 / 2> pixels{};
    if (right < left || bottom < top)
        return pixels;
    const int width = right - left + 1, height = bottom - top + 1;
    const float scale = std::min({ 1.0f, 120.0f / width, 14.0f / height });
    const int drawWidth = std::clamp(static_cast<int>(std::lround(width * scale)), 1, 120);
    const int drawHeight = std::clamp(static_cast<int>(std::lround(height * scale)), 1, 14);
    const int offsetX = (128 - drawWidth) / 2, offsetY = (16 - drawHeight) / 2;
    for (int y = 0; y < drawHeight; ++y) {
        for (int x = 0; x < drawWidth; ++x) {
            const float sx = std::clamp(left + (x + 0.5f) * width / drawWidth - 0.5f, 0.0f, sourceWidth - 1.0f);
            const float sy = std::clamp(top + (y + 0.5f) * height / drawHeight - 0.5f, 0.0f, 15.0f);
            const int x0 = static_cast<int>(sx), y0 = static_cast<int>(sy);
            const int x1 = std::min(x0 + 1, sourceWidth - 1), y1 = std::min(y0 + 1, 15);
            const float upper = std::lerp(source[y0 * sourceWidth + x0], source[y0 * sourceWidth + x1], sx - x0);
            const float lower = std::lerp(source[y1 * sourceWidth + x0], source[y1 * sourceWidth + x1], sx - x0);
            const int intensity = std::clamp(static_cast<int>(std::lround(std::lerp(upper, lower, sy - y0))), 0, 15);
            const uint8_t ia = ((intensity >> 1) << 1) | (intensity > 0);
            const int destination = (offsetY + y) * 128 + offsetX + x;
            pixels[destination / 2] |= ia << (destination % 2 ? 0 : 4);
        }
    }
    return pixels;
}

} // namespace MapTracker

extern "C" int MapTracker_UseNativePausePage(void) {
    return MapTracker::NativePauseEnabled();
}
extern "C" void MapTracker_BeginPauseSession(PlayState* play, int trackerRequested) {
    using namespace MapTracker;
    nativePauseSession = GetDisplayMode() == DisplayMode::Pause3D &&
                         GameInteractor::IsSaveLoaded() && CheckTracker::initialized;
    pauseSessionActive = true;
    previousPausePage = play->pauseCtx.pageIndex;
    restorePausePage = nativePauseSession && trackerRequested;
}
extern "C" void MapTracker_EndPauseSession(PlayState* play) {
    using namespace MapTracker;
    if (restorePausePage)
        play->pauseCtx.pageIndex = previousPausePage;
    pauseSessionActive = nativePauseSession = restorePausePage = false;
    openRequested = closeRequested = rightShoulderPressed = false;
    previewRequested = focusRequested = false;
    capturedPage.reset();
    boundsReady = false;
}

extern "C" void* MapTracker_GetPauseTitleTexture(void) {
    static const std::string fallback = "Map Tracker";
    const auto& state = CheckTracker::mapTrackerState;
    const std::string& title =
        state.tabs.empty()
            ? fallback
            : state.tabs[std::clamp(state.selectedTabIndex, 0, static_cast<int>(state.tabs.size()) - 1)].mapName;
    // Separate immutable buffers keep texture caches and interpolated frames valid across map changes.
    static std::unordered_map<std::string, std::array<uint8_t, 128 * 16 / 2>> titles;
    const auto [entry, inserted] = titles.try_emplace(title.empty() ? fallback : title);
    if (inserted)
        entry->second = MapTracker::BuildPauseTitleTexture(entry->first);
    return entry->second.data();
}
extern "C" void MapTracker_PrepareNativePausePage(void) {
    if (MapTracker::NativePauseEnabled())
        MapTracker::PrepareNativePausePage();
}
extern "C" int MapTracker_ConsumePauseMapRequest(void) {
    const bool requested = MapTracker::openRequested;
    MapTracker::openRequested = false;
    return requested;
}
extern "C" int MapTracker_HandleNativePauseInput(PlayState* play) {
    using namespace MapTracker;
    if (!NativePauseEnabled())
        return false;
    auto& pause = play->pauseCtx;
    auto& input = play->state.input[0];
    if (pause.state == 0) {
        boundsReady = false;
        closeRequested = false;
        rightShoulderPressed = false;
        return false;
    }
    if (pause.state != 6 || pause.unk_1E4 != 0 || pause.debugState != 0) {
        rightShoulderPressed = false;
        return false;
    }
    if (openRequested && pause.pageIndex != PAUSE_MAP) {
        KaleidoScope_SwitchPage(&pause, 2);
    } else if (openRequested) {
        openRequested = false;
    }
    // Shoulder buttons switch native pages; triggers remain available for tracker actions.
    if (rightShoulderPressed && pause.unk_1E4 == 0) {
        restorePausePage = false;
        KaleidoScope_SwitchPage(&pause, 2);
    } else if (pause.unk_1E4 == 0 && CHECK_BTN_ALL(input.press.button, BTN_L)) {
        restorePausePage = false;
        KaleidoScope_SwitchPage(&pause, 0);
    }
    rightShoulderPressed = false;
    if (closeRequested) {
        input.press.button |= BTN_START;
        closeRequested = false;
    }
    if (pause.pageIndex == PAUSE_MAP) {
        input.cur.button &= BTN_START;
        input.press.button &= BTN_START;
        input.rel.button &= BTN_START;
        input.cur.stick_x = input.cur.stick_y = input.rel.stick_x = input.rel.stick_y = 0;
        pause.cursorSpecialPos = 0;
        return true;
    }
    return false;
}

extern "C" void MapTracker_PositionNativePauseButtons(PlayState* play) {
    using namespace MapTracker;
    if (!NativePauseInteractive() || !boundsReady)
        return;
    const auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    const auto interpreter = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (!interpreter)
        return;
    float view[4][4], projection[4][4];
    guMtxL2F(view, play->pauseCtx.view.viewingPtr);
    guMtxL2F(projection, play->pauseCtx.view.projectionPtr);
    // Info-panel vertices use a different projection from the 3D page. Align their visible edges in pixels.
    constexpr float overlapPixels = 3.0f;
    for (int firstVertex : { 8, 12 }) {
        Vtx* vertices = play->pauseCtx.infoPanelVtx + firstVertex;
        const auto left = ProjectPausePoint(
            Vec3f{ static_cast<float>(vertices[0].v.ob[0]), static_cast<float>(vertices[0].v.ob[1]), 0.0f }, view,
            projection, *interpreter);
        const auto right = ProjectPausePoint(
            Vec3f{ static_cast<float>(vertices[1].v.ob[0]), static_cast<float>(vertices[1].v.ob[1]), 0.0f }, view,
            projection, *interpreter);
        if (left && right) {
            const bool leftButton = firstVertex == 8;
            const float targetEdge =
                leftButton ? pagePosition.x + overlapPixels : pagePosition.x + pageSize.x - overlapPixels;
            AlignNativePauseButton(vertices, left->x, right->x, targetEdge, leftButton);
        }
    }
}

extern "C" void MapTracker_DrawNativePausePage(PlayState* play) {
    using namespace MapTracker;
    const auto frame = capturedPage;
    const auto plane = GetPagePlane(play->pauseCtx.mapPageVtx);
    const auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    const auto interpreter = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (interpreter)
        UpdateNativePauseLayoutBounds(play->pauseCtx, plane, *interpreter);
    if (!frame || frame->size.x <= 0 || frame->size.y <= 0)
        return;
    // Retain captured geometry without copying it; each submission owns immutable vertices and commands.
    auto submission = std::make_shared<PageSubmission>();
    submission->geometry = frame;
    submission->vertices.resize(frame->triangles.size() * 3);
    size_t textureChanges = 0;
    const Tile* previousTile = nullptr;
    for (const auto& triangle : frame->triangles) {
        if (triangle.tile.get() != previousTile) {
            ++textureChanges;
            previousTile = triangle.tile.get();
        }
    }
    submission->commands.resize(frame->triangles.size() * 2 + textureChanges * 9 + 16);
    Gfx* display = submission->commands.data();
    gDPPipeSync(display++);
    gSPClearGeometryMode(display++, G_LIGHTING | G_CULL_BOTH | G_ZBUFFER);
    gDPSetCycleType(display++, G_CYC_1CYCLE);
    gDPSetRenderMode(display++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(display++, G_TF_BILERP);
    gDPSetCombineMode(display++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    gSPTexture(display++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    for (size_t index = 0; index < frame->triangles.size(); ++index) {
        const auto& triangle = frame->triangles[index];
        for (size_t i = 0; i < 3; ++i) {
            const auto& source = triangle.vertices[i];
            Vtx& target = submission->vertices[index * 3 + i];
            target.v.ob[0] = std::lround((plane.minimum.x + (source.position.x - frame->origin.x) / frame->size.x *
                                                                (plane.maximum.x - plane.minimum.x)) *
                                         VERTEX_SCALE);
            target.v.ob[1] = std::lround((plane.maximum.y - (source.position.y - frame->origin.y) / frame->size.y *
                                                                (plane.maximum.y - plane.minimum.y)) *
                                         VERTEX_SCALE);
            target.v.ob[2] = 0;
            target.v.flag = 0;
            target.v.tc[0] = std::lround(source.uv.x * 32);
            target.v.tc[1] = std::lround(source.uv.y * 32);
            target.v.cn[0] = source.color.x * 255;
            target.v.cn[1] = source.color.y * 255;
            target.v.cn[2] = source.color.z * 255;
            target.v.cn[3] = source.color.w * play->pauseCtx.alpha;
        }
    }
    const Tile* currentTile = nullptr;
    for (size_t index = 0; index < frame->triangles.size();) {
        const Tile* tile = frame->triangles[index].tile.get();
        if (tile != currentTile) {
            gDPLoadTextureTile(display++, tile->pixels.data(), G_IM_FMT_RGBA, G_IM_SIZ_32b, TILE_TEXTURE_SIZE,
                               TILE_TEXTURE_SIZE, 0, 0, TILE_TEXTURE_SIZE - 1, TILE_TEXTURE_SIZE - 1, 0, G_TX_CLAMP,
                               G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            currentTile = tile;
        }
        // Preserve painter order while loading up to 60 vertices into Fast3D's 64-slot cache.
        size_t end = index + 1;
        while (end < frame->triangles.size() && end - index < 20 && frame->triangles[end].tile.get() == tile)
            ++end;
        const int count = static_cast<int>(end - index);
        gSPVertex(display++, reinterpret_cast<uintptr_t>(submission->vertices.data() + index * 3), count * 3, 0);
        int triangleIndex = 0;
        for (; triangleIndex + 1 < count; triangleIndex += 2) {
            const int v = triangleIndex * 3;
            gSP2Triangles(display++, v, v + 1, v + 2, 0, v + 3, v + 4, v + 5, 0);
        }
        if (triangleIndex < count) {
            const int v = triangleIndex * 3;
            gSP1Triangle(display++, v, v + 1, v + 2, 0);
        }
        index = end;
    }
    gSPEndDisplayList(display++);
    submission->commands.resize(static_cast<size_t>(display - submission->commands.data()));
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(1.0f / VERTEX_SCALE, 1.0f / VERTEX_SCALE, 1.0f / VERTEX_SCALE, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, submission->commands.data());
    Matrix_Pop();
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_DISPS(play->state.gfxCtx);
    submittedPages.push_back(std::move(submission));
    while (submittedPages.size() > 4)
        submittedPages.pop_front();
}
