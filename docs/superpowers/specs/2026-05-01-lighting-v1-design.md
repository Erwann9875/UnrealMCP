# Lighting V1 Design

## Goal

Add editor-only lighting and night-atmosphere tools so generated Unreal scenes can be turned into readable night city scenes without hand-editing sun, moon, sky, fog, exposure, or bulk lights.

## Scope

This slice adds a compact lighting surface:

- `lighting.set_night_scene`
- `lighting.set_sky`
- `lighting.set_fog`
- `lighting.set_post_process`
- `lighting.bulk_set_lights`
- `lighting.set_time_of_day`

`lighting.set_night_scene` is the high-level preset. It configures one moon directional light, disables competing directional lights to avoid forward-shading warnings, ensures sky/fog/post-process support actors exist, and returns a compact summary.

`lighting.bulk_set_lights` accepts many point, rect, or spot lights in one call and returns only names, paths, kinds, and counts. This supports streets, skyscraper windows, signage, and cinematic accents without one MCP call per light.

## Architecture

The Rust protocol gains lighting commands and compact result structs. The MCP stdio layer exposes Codex-compatible object schemas with no top-level schema composition keywords. The Unreal editor bridge performs all actor creation and lighting changes on the game thread, reuses named support actors where possible, and tags generated lighting actors with `mcp.generated` and `mcp.lighting`.

## Constraints

- Editor-only, Unreal Engine 5.5+.
- Bulk-first: city light placement must be possible in one request.
- Token-efficient responses: return counts and compact actor summaries, not full component state.
- Avoid duplicate forward-shading directional-light warnings by using one active moon/sun light at a time.
- Do not add runtime LED animation here; this slice prepares lighting for later runtime/Blueprint work.
- Tool schemas must stay compatible with Codex function parameter validation.

## Defaults

- Night moon rotation: `[-35.0, -25.0, 0.0]`.
- Night moon intensity: `0.12`.
- Night moon color: `[0.55, 0.65, 1.0, 1.0]`.
- Night sky intensity: `0.05`.
- Night fog density: `0.01`.
- Night exposure compensation: `-0.5`.
- Bulk light default kind: `point`.
- Bulk light default color: `[1.0, 0.82, 0.55, 1.0]`.
- Bulk light default intensity: `5000.0`.
- Bulk light default attenuation radius: `1000.0`.

## Testing

Rust tests cover protocol round trips, tool listing schemas, and representative MCP calls through the fake bridge. Unreal verification builds the plugin and runs live smoke calls against `MCPTester`: configure a night scene, create representative lights in bulk, query lighting actors, and clean up the smoke-specific lights.
