# Landscape Grounding v1 Design

## Goal

Add editor-only MCP support for creating real Unreal landscapes and grounding generated actors on them, so generated cities no longer rely on floating blocks or a manually prepared floor.

## Scope

This slice adds four high-value tools:

- `landscape.create`: create or update one named Unreal `ALandscape` actor with a flat heightfield.
- `landscape.set_heightfield`: rewrite a landscape heightfield from compact procedural parameters or explicit normalized samples.
- `landscape.paint_layers`: assign a landscape material and register named paint layers so the landscape is ready for texture/layer workflows.
- `placement.bulk_snap_to_ground`: snap many actors selected by names or tags onto the first blocking surface below or above them.

Road splines, water bodies, and scatter placement are intentionally deferred. They depend on a reliable terrain surface and can be added cleanly after this slice.

## Architecture

The Rust protocol gets landscape and placement command/result structs, then the MCP stdio layer exposes object-only JSON schemas and dispatches to the bridge in one request per tool call. The Unreal bridge owns all editor operations in `BridgeServer.cpp`, following the current bulk-first pattern used by world/material/lighting commands.

Landscape creation uses `ALandscape::Import` with generated height data sized from component counts. Height updates use `FLandscapeEditDataInterface::SetHeightData`, avoiding per-vertex MCP chatter. Placement snapping uses a single line trace per actor and returns compact per-actor summaries.

## Data Model

`LandscapeCreateSpec` carries name, component counts, section settings, transform, optional material, and tags. `LandscapeHeightPatch` carries a target landscape name, dimensions, base height, procedural hill/mountain controls, optional city pad flattening, and optional explicit normalized samples. `LandscapeLayerPaint` carries landscape name, optional material, and layer names. `PlacementSnapSpec` carries names/tags plus trace distance, vertical offset, and whether generated actors are included.

## Error Handling

Rust validates JSON shape through serde and keeps MCP schemas free of top-level `anyOf`, `oneOf`, `allOf`, and `not`. The Unreal bridge returns structured bridge errors for missing worlds, invalid dimensions, missing landscapes, invalid samples, missing materials, and no selectable actors.

## Testing

Protocol tests cover request/result round-trips. MCP stdio tests cover tool listing, schema shape, and dispatch through the fake bridge. Unreal verification compiles the plugin with UE 5.7, builds `MCPTesterEditor`, and runs a live smoke test that creates a landscape, applies a heightfield, paints layers/material, spawns an actor, snaps it to the ground, queries it, and cleans it up.
