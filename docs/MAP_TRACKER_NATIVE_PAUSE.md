# Native Pause Map Prototype

Select **In-Game Map Mode** in Randomizer > Map Tracker settings:

- **Fullscreen 2D**: the standalone animated panel, with the committed margins, rounded
  corners, persistent sidebar and independent gameplay pause. Opening from vanilla pause returns
  to that same pause menu when closed.
- **Native Pause 3D** (default): the tracker replaces the vanilla Map page and follows its real page rotations.

The classic ImGui tracker remains available and is not replaced. Existing `NativePausePage` preferences
are retained. A mode change closes an open standalone panel and releases its audio/input lock.
Changes during a vanilla pause are deferred until that pause closes: rendering and input ownership
never switch halfway through a pause animation.
Changing views resets page/input state without discarding loaded image registrations; the pack loader
owns their cleanup on reload or game teardown.

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
- Check totals are centered in the native header, away from the pause HUD's corner buttons.
- The native zone title is inset by one space, emboldened and 10% larger; other text keeps its size.
- The native footer controls are centered on one row; long custom bindings reduce only the footer's
  font size to fit. Other text and the classic/standalone tracker layout are unaffected.
- The native info panel reads **Map Tracker**, composed from original English pause-name glyphs in
  a 128x16 IA4 texture. Other pause pages retain their original labels.
- On the settled Map page, L/R icon edges are aligned to the projected panel edges with approximately
  three screen pixels of overlap. Other pause pages retain their original icon positions.
- Manual navigation (including Overworld) is preserved until Focus Player or the next pause session.
- Focus Player changes the map without moving the cursor. The cursor and detail panels remain within
  the native page's projected bounds.
- The classic tracker is not rendered during native pause sessions, so its tabs and mouse hover cannot
  interfere with the shared map selection and popup state. Its visibility setting is preserved.
- Multicheck lists, requirements, logic and hints remain readable 2D windows. They are only submitted
  when Map is the settled, active page, never during a page rotation or pause transition.

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
three panel widths, and verify centering, one-line layout and unchanged standalone alignment.
List checks also cover mouse/controller row selection and opaque background vertices. Layout-bound
checks compare opening and settled poses at three viewport sizes and verify matrix-stack restoration.
They do not replace in-game visual tests or macOS/Linux builds. Check mouse/controller targeting,
multicheck hover, text legibility,
zoom quality and frame time before distributing this prototype.
