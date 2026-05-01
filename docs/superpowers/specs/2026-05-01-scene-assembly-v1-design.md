# Scene Assembly v1 Design

## Goal

Add editor-only scene assembly commands that let an LLM create useful city-scale layouts with a few compact MCP calls instead of thousands of individual spawn calls.

## Scope

Scene Assembly v1 adds four commands:

- `road.create_network` creates a grid road network from cube mesh segments.
- `scene.bulk_place_on_grid` places repeated static mesh actors with deterministic spacing, yaw, and scale variation.
- `scene.create_city_block` creates one block with perimeter roads, sidewalks, and a building grid.
- `scene.create_district` creates a preset multi-block district such as `downtown`, `residential`, `industrial`, `beach`, or `hills`.

All tools are editor-only and return compact counts plus actor names/paths. They use existing static mesh assets, defaulting to `/Engine/BasicShapes/Cube.Cube` so the tools work without importing assets first.

## Protocol

The Rust protocol adds spec structs for road networks, grid placement, city blocks, and districts. The shared result type is `SceneAssemblyResult`, containing `spawned`, `count`, `road_count`, `sidewalk_count`, `building_count`, and `prop_count`.

Command names follow the existing pattern: MCP dotted names map to bridge snake names, for example `scene.create_city_block` maps to `scene_create_city_block`.

## Unreal Bridge

The bridge generates actors in one command and one transaction. It reuses the existing static mesh actor spawning pattern, applies `mcp.generated`, `mcp.scene:<scene>`, `mcp.group:<group>`, and type tags such as `mcp.scene_actor:road`.

Roads and sidewalks are cube actors scaled in centimeters. Buildings and props are user-supplied mesh actors. District generation is deterministic from a seed and preset, so repeated calls produce stable layouts.

## Error Handling

The bridge rejects missing editor worlds, invalid arrays, and missing meshes with existing `invalid_payload` or `unreal_api_failure` errors. Per-actor failures are skipped only when a generated position is invalid; missing required meshes fail the whole command so broken layouts are visible.

## Testing

Rust protocol roundtrip tests cover request/result serialization. MCP stdio tests cover tool listing, schemas, and fake bridge dispatch. Unreal verification uses BuildPlugin plus a live smoke test that creates a level, runs all four tools, validates spawned actors by tags, and deletes them.
