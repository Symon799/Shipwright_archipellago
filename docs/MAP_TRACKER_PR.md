# Map Tracker PR Overview

## Purpose

This PR adds a map-based randomizer tracker to Ship of Harkinian.

The goal is to make randomizer progression easier to read in-game, especially for players who are not already comfortable with routing and external tracking tools.

## Why This Is Valuable For SoH

The value of this feature is not just that it is a map tracker. The value is that it is an in-game map tracker connected directly to SoH's live randomizer state.

Compared to a typical external tracker workflow, this implementation can do things that are specific to the running game:

- update check live as inventory, age, and time of day change
- show age/time mismatch states directly in the tracker
- evaluate map-link entrance logic and reflect it visually
- auto-focus the current player area
- support direct map interaction with zoom and navigation inside the tracker UI

This makes the randomizer easier to use without changing how the randomizer itself works.

## Player-Facing Features

The map tracker adds the following behavior:

- checks are drawn directly on maps as markers and marker color reflects check state
- markers support grouped multi-check displays when several checks share a position
- hovering a check shows requirement text and optional logic text
- right-clicking a check can reveal a pack-provided hint
- links between maps can be drawn as dedicated round markers
- links show the destination map and preview its contained checks on hover
- link borders can reflect entrance availability logic and age/time mismatch
- the tracker can auto-focus the current player area
- the tracker supports zooming and panning on maps
- the tracker can reload map-pack data without restarting the game

The feature ships with a set of map resources, but it is not locked to a single pack. It is data-driven, and custom packs can be authored with the companion tool here:

- https://github.com/Symon799/SoH_Map_Ressource_Maker

That makes the feature useful both as a built-in tracker and as a flexible base for community-made map packs.

## Design Goals

The implementation was guided by a few clear goals:

- exact mapping between pack data and in-game checks
- good runtime performance
- maintainable code structure
- support for community-authored map packs


## Technical Overview

At a high level, the map tracker works as a data-driven layer on top of the existing randomizer and tracker state.

It does not hardcode every check position in code. Instead, it loads a pack that describes:

- map tabs
- images
- links between maps
- check placements
- optional hints

The game then resolves those pack entries to real in-game randomizer checks and renders them in the tracker UI.

## Main Components

The implementation is split by responsibility.

### `randomizer_check_tracker.cpp`

This remains the integration point with the existing tracker system and settings UI.

It owns the tracker window entry points and calls into the map tracker when map mode is enabled.

### `map_tracker_core.cpp`

This file owns the data-loading and state-building path.

It is responsible for:

- finding the active map-pack zip
- mounting the archive
- loading `maps.json`
- loading area files
- validating pack structure
- mapping pack check ids to real in-game checks
- building tabs, markers, groups, links, and warnings
- loading textures for map images
- updating player auto-focus state

### `map_tracker_render.cpp`

This file owns rendering and rendering-only cache logic.

It is responsible for:

- drawing map tabs and grouped tabs
- drawing markers and links
- building tooltip content
- rendering multi-check popups
- rendering link popups
- caching derived render data so it is not rebuilt every frame

### `map_tracker_link_resolver.cpp`

This file owns map-link entrance resolution.

That logic used to live inside the core loader. It is isolated so that link-to-entrance behavior has clear ownership and is easier to maintain.

### `randomizer_check_ids.cpp`

This file is the runtime source of truth for map tracker `soh_id` values.

It provides the exact stable ids used to match pack checks to in-game checks.

### `location.cpp`

Each `Location` exposes its canonical map tracker id through `Location::GetMapTrackerId()`.

This removes the need to use display names or token matching so the system can use exact ids for runtime mapping.

### Check Identity

Each pack check uses:

- `soh_id`

This maps directly to a real in-game `RandomizerCheck` through the id table.

This means:

- check display names can change without breaking packs
- pack check editor names can change without breaking packs
- runtime matching is deterministic

### Map Identity

Maps are also identified by exact ids.

The pack uses:

- `id` for map identity
- `map_id` for marker placement
- `target_map_id` for links between maps

This also avoids using display names as runtime keys.

## Pack Format

The map tracker expects a zip pack under:

- `mods/check_tracker_map_pack`

When multiple zips are present, the tracker chooses the newest modified zip.

The pack contains:

- `maps.json`
- area files under `areas/`
- map images referenced by `maps.json`

### `maps.json`

Each entry defines a map tab:

- `id`
- `name`
- `img`
- optional `group`
- optional `links`

### Area Files

Each area file contains:

- a `checks` array

Each check entry contains:

- `soh_id`
- optional `name`
- optional `hint`
- `map_locations`

Each `map_locations` entry contains:

- `map_id`
- `x`
- `y`
- optional `size`

The runtime display name shown to the player comes from the real in-game check, not from the pack check name. The pack name currently is just editor/debug metadata.

## Load Flow

The runtime flow is:

1. Find the map-pack folder.
2. Pick the newest modified zip.
3. Mount the zip in the archive manager.
4. Load `maps.json` from the mounted archive.
5. Validate and build map metadata.
6. Load and validate area files from the archive.
7. Resolve `soh_id` entries to real in-game checks.
8. Build marker and link state.
9. Load map textures from archive resources.
10. Swap the finished state into the tracker.


## Strict Schema Validation

The loader validates the current pack format.

Malformed required fields are not silently tolerated. Instead, the loader records clear warnings and aborts reload when the schema is invalid.

This improves reliability because bad pack data fails fast and visibly instead of producing confusing partial behavior.

Examples of validated required fields include:

- map `id`
- map `name`
- link `target_map_id`
- check `soh_id`
- check `map_locations`
- placement `map_id`
- placement coordinates

## Render Cache

The tracker uses a render cache so that expensive derived UI state does not need to be rebuilt every frame.

The cache stores derived data such as:

- per-tab visual summaries
- renderable marker lists
- clustered multi-check marker data
- link availability summaries

The cache is invalidated when relevant gameplay or tracker state changes.

This keeps the UI responsive while still allowing the tracker to reflect live check-state changes.

## Link Logic

Map links are not only visual navigation shortcuts. They can also reflect entrance logic.

When a link can be resolved to a real in-game entrance, the tracker can evaluate:

- whether that entrance is currently usable
- whether it is usable only under different age/time conditions

This lets the tracker communicate progression constraints directly on map transitions.

For ambiguous cases, such as multiple links from one source map to the same target map, the logic evaluation is intentionally disabled rather than guessed.

## Auto-Focus

The tracker can auto-focus the map tab that best matches the player's current area or current scene.

This makes the feature much easier to use in normal gameplay because the player usually does not need to manually hunt for the correct map.

Special-case handling is included for scenes that do not map cleanly to a single area-tab relationship.

## Summary

This PR adds an in-game map tracker that helps players understand randomizer progression spatially and interactively.

It is especially valuable for less experienced randomizer players, who benefit the most from seeing checks and progression directly on maps instead of reconstructing that information mentally or through external tools.

Technically, the implementation is built around exact ids, archive-driven pack loading, modular ownership, and cached rendering.

That combination makes the feature both player-friendly and maintainable.
