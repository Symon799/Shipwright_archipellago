# Experimental Player Position

Enable **Randomizer > Map Tracker > Experimental Player Position** to show an
approximate player marker in both map views. This option is off by default.
The turquoise arrow shows facing; `~` identifies an approximation. It appears
inside the current local area's map, not on the Overworld overview.

Lost Woods, Haunted Wasteland and Lake Hylia are explicitly excluded in both
views, even if a pack supplies calibration profiles. The exclusion also covers the isolated
Kokiri/Hyrule bridge belonging to the Lost Woods scene. Other areas retain their
existing behavior. Further editor calibration and dungeon registration work is
on hold; the optional pack format remains supported. Existing packs do not need
new fields or edited check/link coordinates.

## Current Scope

The default path is automatic approximation from outdoor scene exits and pack
links. It is useful on some maps, but is not guaranteed to match an artistic
tracker image accurately. No editor implementation or further dungeon calibration
work is planned for now.

Native dungeon minimaps exist, but are split into room/floor references, unlike
tracker maps which can combine and rearrange multiple rooms and floors in one
image. The current calibration selectors do not distinguish stacked floors of
the same room. Choosing anchors by proximity or loaded geometry would not resolve
that ambiguity reliably.

Map selection is separate from the position arrow: Focus Player and automatic
area selection still work when no arrow can be drawn. Link's house explicitly
focuses Kokiri Forest, including at initial spawn.

## Pack Calibration

Maps can optionally supply version 1 `player_position.calibrations` in `maps.json`.
Each profile pairs world X/Z coordinates with native tracker image pixels, using
at least three distinct non-collinear anchors. There is no fixed anchor limit.
Profiles select a scene and optionally a room, setup list and vanilla/MQ layout.
The most specific matching profile wins (room 4, setups 2, layout 1); equal scores
are ambiguous and fall back to automatic registration.

`piecewise_affine` triangulates the source anchors with Delaunay and interpolates
inside each triangle, allowing local map deformation. `affine` uses one global
fit. Facing follows the same local transform. Degenerate or folded meshes are
rejected. `outside_hull: hide` (default) hides the player outside the calibrated
region; `affine` explicitly enables global extrapolation when a usable fit exists.

Old packs remain valid without this optional field. Missing, invalid, unsupported
or unmatched profiles fall back to the existing link approximation. A valid
profile with `outside_hull: hide` does not fall back outside its hull.

### Retained Version 1 Contract

The runtime already supports this optional field on each entry in `maps.json`.
The following anchors are synthetic examples, not a real Kokiri calibration:

```json
"player_position": {
  "version": 1,
  "calibrations": [{
    "id": "kokiri_forest.default",
    "reference_id": "kokiri_forest.default",
    "scene_id": 85,
    "room_id": null,
    "setup_ids": null,
    "layout": "any",
    "method": "piecewise_affine",
    "outside_hull": "hide",
    "anchors": [
      {"id": "origin", "world": {"x": 0, "z": 0}, "map": {"x": 100, "y": 100}},
      {"id": "east", "world": {"x": 1000, "z": 0}, "map": {"x": 300, "y": 100}},
      {"id": "south", "world": {"x": 0, "z": 1000}, "map": {"x": 100, "y": 300}}
    ]
  }]
}
```

- `scene_id` is the game's scene ID, not a minimap index.
- `room_id: null` matches all rooms; an integer matches one room.
- `setup_ids: null` matches all scene setups; otherwise use a nonempty integer list.
- `layout` is `any`, `vanilla` or `mq`; the default is `any`.
- `world` stores finite world X/Z coordinates; `map` stores finite native tracker
  image pixels, origin top-left, X right and Y down, independent of zoom.
- `method` defaults to `piecewise_affine`; `affine` uses a global fit.
- `outside_hull` defaults to `hide`; `affine` permits global extrapolation only
  when a usable global fit exists.
- `reference_id`, anchor IDs and optional labels are editing metadata, not
  resources required by the runtime.

Excluded scenes remain excluded regardless of profile contents. Invalid profiles
produce warnings without preventing pack loading. The mesh is built when the
pack is parsed, not on every frame. Source anchors must be distinct and provide
non-degenerate triangles; consistent mirrored geometry is supported.

If editor work resumes, keep calibration data even when "preserve unknown fields"
is disabled. Paired source/target anchors can be edited without moving any check
or link. Convert clicks on a reference image to world X/Z by inverting its
`world_to_image` matrix; preserve floating-point precision and native coordinates.
Do not add official reference images to exported packs.

## Local Reference Export (Optional Tooling)

Run `export-map-references.bat` with Python 3.10+ to export 24 outdoor minimaps
from local `x64/Debug/oot.o2r` to `build/map_tracker_references`. Alternatively:

```powershell
python scripts/export_map_tracker_references.py --archive path/to/oot.o2r --output path/to/references
```

The output contains `position_reference.json` and an `images` directory. These
cover 20 outdoor scenes plus four visual variants; variant names are reference
metadata, not additional runtime profile selectors. The native PNGs use cyan
on a dark background for readability, without HUD offsets or cosmetic mirroring.

Each manifest entry includes an ID, map ID, scene/context, relative PNG path,
native `image_size`, and a 2x3 `world_to_image` matrix applied to `[world_x,
world_z, 1]`. Output coordinates are native top-left image pixels. `samples`
is currently empty: in-game sample capture is not implemented.

Lost Woods and Wasteland textures are essentially uniform placeholders and are
not usable visual calibration references. Lake Hylia has a map, but its tracker
projection was judged insufficiently reliable. Their exported PNGs do not enable
position tracking in these excluded scenes.

Official images remain local editing references, never repository or public CI
artifacts. Only calibration coordinates would ship with a pack. Dungeon room/floor
reference export is not implemented. If an importer is added later, validate
image dimensions and invertible finite matrices, and reject unsafe relative paths
or ZIP traversal entries.

## Automatic Registration (Fallback)

The tracker reads the scale and offset used by the game's minimap compass.
These convert Link's world X/Z position into local minimap pixels, independent
of HUD placement or visibility. The minimap does not contain a complete list of
named exits; the active scene's exit surfaces supply those correspondences:

1. Calculate the area-weighted center of each exit surface.
2. Convert it to minimap coordinates and resolve its destination to a tracker
   map ID. The Lost Woods bridge is distinguished from the forest proper: the
   pack represents it as the connection between Kokiri Forest and Hyrule Field.
3. Match that destination to a unique link in the current map's resource pack.
4. Fit an affine transform using at least three non-collinear anchors, or a
   rotation/uniform-scale transform using two anchors.
5. Convert the player's position and facing to minimap coordinates, then
   transform them into the local tracker image with that same fit.

The fit is cached for the current scene, active exit list and resource pack.
No map coordinates, pack changes, extra assets or editor changes are required.

## Limits

- Automatic registration supports outdoor scenes with a native minimap. Dungeons need
  a separate room/floor registration and interiors often have no native minimap.
- Fewer than two matching exits cannot determine a transform.
- Duplicate links or exits to the same destination are excluded: the pack does
  not identify which physical exit corresponds to which link.
- A two-anchor fit cannot infer reflection or isometric deformation. Additional
  independent anchors improve the result.
- Artistic maps, rearranged rooms, multiple elevations and non-linear layouts
  cannot be accurately registered with one affine transform.
- Poor fits, collapsed transforms and positions outside the image are hidden
  rather than clamped to a misleading point. Entrance randomization or modified
  scene assets can also prevent a match.

Test outdoor areas with several distinct links first (Kokiri Forest or Hyrule Field),
moving and turning near their exits. Compare both map views,
zoom levels and age-dependent scene variants. This is not a navigation guarantee.
