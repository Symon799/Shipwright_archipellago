# Native Pause Map Prototype

Select **Display Mode** in Randomizer > Map Tracker settings. The four modes are mutually exclusive:

- **In-Game Pause 2D**: the standalone animated panel, with the committed margins, rounded
  corners, persistent sidebar and independent gameplay pause. Opening from vanilla pause returns
  to that same pause menu when closed.
- **In-Game Pause 3D** (default): the tracker replaces the vanilla Map page and follows its real page rotations.
- **Windowed**: mouse-driven, resizable classic tracker, controlled by the menu toggle.
- **Floating Overlay**: the classic tracker without a title bar, also controlled by the menu toggle.

The classic tracker is hidden in the two pause modes. Its menu toggle is disabled with an explanatory
tooltip; use the mapped Map Tracker button for pause modes. Choosing Windowed/Floating opens the
classic tracker. `DisplayMode` is the single preference; legacy `NativePausePage`, `InGameEnabled`
and `WindowType` preferences determine the initial choice until the new setting is saved.
A mode change closes an open standalone panel and releases its audio/input lock.
Changes during a vanilla pause are deferred until that pause closes: rendering and input ownership
never switch halfway through a pause animation.
Changing views resets page/input state without discarding loaded image registrations; the pack loader
owns their cleanup on reload or game teardown.
On application shutdown, the image worker is joined and all captured pages, texture registrations
and the cached custom background are released before the rendering context and logger are destroyed.
On Windows/DXGI, the main HWND is detached from the engine's event callback before Context teardown,
then destroyed afterwards. This prevents late focus/destruction messages from accessing a dead Context
when quitting with either pause tracker open, without modifying the libultraship submodule.
The native popup viewport is a geometry-only proxy: it never copies ImGui's platform/renderer
ownership pointers, so its static destructor remains safe after the ImGui context is destroyed.

## Behaviour

- The tracker toggle opens the vanilla pause directly on its Map page, focused on the player's area.
- Player focus is applied at the start of opening, before rendering the animation. The previous page
  capture is discarded; a non-interactive preview is captured during opening, without 2D popups or cursor.
  If metadata is still loading, focus is applied once it becomes available, before drawing the map.
- Start still opens and closes the ordinary pause menu. Its other pages keep their original behaviour.
- If the tracker shortcut opened pause from gameplay, closing without L/R page navigation restores
  the previously remembered page for the next Start. Deliberate L/R navigation and ordinary Start
  sessions retain vanilla page memory.
- Toggle defaults to Select/View on the controller and M on the keyboard. Existing custom bindings
  are preserved. LB/RB switch pages while paused; LT/RT remain available for tracker actions.
- Select, Back, Focus Player, Overworld, zoom and list navigation retain their tracker bindings.
- In 3D mode, holding Zoom In/Out repeats after 0.3 seconds, then every 0.12 seconds. Releasing the
  button resets the delay; opposing zoom buttons cancel each other. The 2D panel keeps single-step zoom.
- List Up/Down opens a list over the full-width map, without changing its scale or pan offset.
  Mouse click or controller Select activates the hovered map row before dismissing it. Moving the
  mouse/controller cursor back into the uncovered map closes it
  immediately. The list blocks interaction with map markers beneath it.
- Moving the mouse/controller cursor to the left edge of the native panel also opens the list.
- List rows highlight under the custom controller cursor, not a stationary mouse pointer.
- The native header displays centered white **MAP TRACKER**, emboldened and 40% larger than normal text,
  without increasing the header height. The map name appears only in the gray info panel below.
  The title has a thicker black outline. Hovering it with the mouse or controller cursor shows
  check totals in an opaque 2D panel centered underneath, hidden during page animations. Totals never
  move controls or resize the map. All tracker modes use **Accessible / Checked / Total** order.
  Temporary totals-position settings have been removed.
  Totals are always enabled in every tracker mode; the old Show Check Totals option is removed.
  Map Tracker font size is limited to 0.3-1.3 in settings and rendering, including saved oversized values.
  Collapsing the classic tracker still closes its ImGui window/style stacks correctly.
- Footer controls are 15% larger than normal text; popup, list and map text keep their size. The native select label
  reads **Select**, while the standalone panel retains **Select / Hint**.
- Totals/footer text has a stronger two-pixel dark shadow, with near-white totals and footer labels for contrast
  over the custom texture. The map image has a thin warm border and a small offset shadow. These are
  native-only decorations and do not change map sizing, zoom, marker positions or interaction bounds.
- The two main horizontal separators are black and three pixels thick in native mode only.
- Native and standalone 2D footer controls are centered on one row. Long custom bindings reduce only
  the native footer's font size to fit; 2D font size and styling remain unchanged. The classic window
  is unaffected.
- The native gray info panel displays the selected map's name using the game's font in a 128x16 IA4
  texture. Text is centered with inner margins and long names are scaled down to fit. Immutable cached
  textures update on map selection without stale text or per-frame rasterization. Other pause pages
  retain their original labels; empty metadata falls back to **Map Tracker**.
- On the settled Map page, L/R icon edges are aligned to the projected panel edges with approximately
  three screen pixels of overlap. Other pause pages retain their original icon positions.
- Manual navigation (including Overworld) is preserved until Focus Player or the next pause session.
- Focus Player changes the map without moving the cursor. The cursor and detail panels remain within
  the native page's projected bounds.
- The classic tracker is not rendered during native pause sessions, so its tabs and mouse hover cannot
  interfere with the shared map selection and popup state. Its visibility setting is preserved.
- Multicheck lists, requirements, logic and hints remain readable 2D windows. They are only submitted
  when Map is the settled, active page, never during a page rotation or pause transition.
- The native cursor is always drawn in the foreground while Map is interactive, including when only
  its outer ring overlaps a detail panel. It is not also drawn on the 3D page.
- Requirements already displayed beside a popup row are omitted from its tooltip. Hint text/prompts
  and optional logic remain available; no secondary tooltip opens when it would contain nothing new.

## Classic Tracker And Tools

The classic window uses compact, centered group/map button rows with dark fills, gold selection
and thin status-colored underlines. Focus is pinned to the left and compact Accessible / Checked / Total
counters to the right of the group row, with matching action colors/borders; hovering the counters
shows their full labels. Marker size, colors and mouse actions
are unchanged. Nested windows apply the configured font scale only once; popup lists do not add
another padding layer, allowing names and inline requirements/items to fit the measured row width.

Below Display Mode, settings are split into Map Content and Appearance. Mode-specific settings
are disabled with explanations when not applicable. Data & Diagnostics contains only
Open Map Tracker Debug Tools. The Debug checkbox (off by default; saved preferences are honored)
and Reload Map Data are inside that separate diagnostics window.
Debug tooltips include the same tracker ID and pack name for single checks and multicheck rows
in all three views. Opening the diagnostics window alone does not enable marker debug.
Move Floating Overlay is explicitly floating-only and disabled for normal windows. The old
Only Enable While Paused option is removed. Map Tracker Debug contains the assets path, marker
diagnostics, reload, tracker visibility/export controls and Unlinked / Issues, without changing
the normal tracker's selected group or adding a diagnostics tab to its navigation.
Selecting Windowed/Floating disables both integrated modes. A native pause retains its renderer/input
owner until closing, after which new pauses use the original map page. Switching back requires release
before the next toggle to avoid immediately reopening from a held button.

In both integrated modes, mapped pan/zoom remains available while the cursor is over a multicheck
or link panel, including outside the image. Right stick pans the map instead of scrolling popup
rows. Mouse-wheel scrolling stays with the popup, while mapped zoom buttons still affect the map.
The map sidebar retains its existing input capture and scrolling behavior.

## Rendering

`map_tracker_pause.cpp` translates the existing page layout into native textured triangles. The page
uses Kaleido's model/view/projection matrices, rather than simulating the rotation in a 2D overlay.
Only the standalone detail windows are submitted through the ordinary ImGui renderer.

Native textures retain the pack's full resolution. 254x254 image tiles with a one-pixel gutter are
uploaded through Fast3D's existing LoadTile path as 256x256 RGBA textures, without changing libultraship.
The gutter prevents filtering seams. Source pixels share ownership with the decoded GUI texture;
tiles are generated lazily and reused, avoiding an additional full-image copy or thousands of tiny uploads.
The rendered geometry uses the actual vanilla page background vertices, including its animated vertical
offset. Layout bounds are calculated at the final Map-page pose before the first capture, so the layout
does not resize when opening finishes; the original 3D page animation remains intact.
The native layout is square, dark red, with minimal horizontal padding.
Only the main panel uses a 97%-opaque red fill with a thin outer border, letting the existing
textured page show through very slightly. The map list, multicheck/link panels
and detail tooltips retain opaque backgrounds. Text, separators, markers and images remain unchanged.
Adjust `NATIVE_PAUSE_BACKGROUND_OPACITY` in `randomizer_map_tracker.cpp` (0 = transparent, 1 = opaque)
and rebuild to change this opacity. There is no transparency setting in the menu; 2D/classic modes are unaffected.

**Use Custom Background (3D)** replaces only the main panel's color fill with the bundled
`textures/map_tracker/pause_map_tracker.png`. It is on by default; saved user preferences are retained.
Missing or invalid image resources
fall back to the existing colored panel. The image follows the actual page's native 3D rotations,
removes the outer window border and does not affect list/popup backgrounds or the 2D/classic tracker.
Color mode keeps its thin window border. The map image's own border is present in both background modes.
The image is decoded/uploaded once and its native registration survives pack reloads without a second
GPU upload. Edit `soh/assets/custom/textures/map_tracker/pause_map_tracker.png` to adjust it, preserving
its 3:2 aspect ratio. Regenerate `soh.o2r` with the `GenerateSohOtr` target after editing; the CI resource
job bundles it automatically. No changes to the map resource pack or libultraship are needed.
Display-list submissions retain their vertices and textures across interpolated frames. Retired tile
addresses are evicted from the renderer's texture cache before their memory is reused.
Consecutive triangles using the same texture share one LoadTile command, rather than reloading it
per triangle (which flushes Fast3D's drawing batch). Vertex loads group up to 20 triangles and use
paired triangle commands without changing painter order. Fully visible, single-tile triangles bypass
polygon clipping, and submissions retain captured geometry instead of copying it.
Window capture follows the current frame's parent/child hierarchy, not the global window list which
ImGui sorts later at EndFrame. Newly created map windows therefore stay beneath the map list from
their first frame.

## Validation

Focused local checks exercise pause eligibility, page switching, tracker input isolation, Start
closing, unchanged inputs on other pages, triangle clipping, page edges/offsets, full-resolution tile
gutters, bounded popup placement and classic/native state isolation. Navigation checks preserve
Overworld for 100 consecutive native draws and verify that explicit focus and classic scene auto-focus
still work. These checks use extracted production functions with stubbed game/rendering dependencies;
the overlay check also runs the actual list/canvas layout against a headless ImGui context and verifies
unchanged canvas bounds, immediate dismissal and input isolation. Synthetic draw-list checks decode
the generated GBI commands to verify texture-run counts, vertex cache limits and painter order.
Additional checks reproduce a new map child being appended after an existing list in the global
window order, and verify its correct position in the captured hierarchy. Opening checks exercise
early focus, stale capture retirement, delayed metadata and button edge alignment at multiple scales.
Mode checks cover deferred selection, delayed tracker initialization, shortcut page restoration and
held zoom in both directions. Footer layout checks use real ImGui with keyboard/controller labels at
three native panel widths, and verify centering, one-line layout and centered standalone keyboard/controller
footers with unchanged font size and styling.
List checks also cover mouse/controller row selection and opaque background vertices. Layout-bound
checks compare opening and settled poses at three viewport sizes and verify matrix-stack restoration.
They do not replace in-game visual tests or macOS/Linux builds. Check mouse/controller targeting,
multicheck hover, text legibility,
zoom quality and frame time before distributing this prototype.
