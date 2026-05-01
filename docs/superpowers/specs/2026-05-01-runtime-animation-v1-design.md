# Runtime Animation V1 Design

## Goal

Add editor-only Blueprint and runtime-animation tools so generated Unreal scenes can create reusable actor Blueprints and make lights/materials animate during Play-in-Editor without raw Blueprint graph editing.

## Scope

This slice adds a compact Blueprint/runtime surface:

- `blueprint.create_actor`
- `blueprint.add_static_mesh_component`
- `blueprint.add_light_component`
- `blueprint.compile`
- `runtime.create_led_animation`
- `runtime.create_moving_light_animation`
- `runtime.create_material_parameter_animation`
- `runtime.attach_animation_to_actor`

Blueprint tools create actor Blueprint assets and add simple component templates in bulk-friendly calls. Runtime tools create small animation preset assets and attach those presets to placed actors or Blueprint assets through an Unreal MCP runtime animator component.

## Architecture

The Rust protocol gains Blueprint and runtime animation command/result types. The MCP stdio layer exposes object schemas with compact defaults and no top-level schema composition keywords. The Unreal bridge implements editor asset mutation using Blueprint/SCS APIs and adds two bridge-owned UObject types:

- `UUnrealMCPRuntimeAnimationPreset`: a data asset describing one high-level animation.
- `UUnrealMCPRuntimeAnimatorComponent`: a tick component that applies presets at runtime.

The animator component is intentionally high-level. It supports pulsing material vector/scalar parameters, moving selected light components along an axis, and pulsing light intensity/color. This avoids emitting or editing Blueprint graph nodes from the MCP and keeps LLM payloads small.

## Constraints

- Editor-only, Unreal Engine 5.5+.
- Play-in-Editor behavior is in scope; packaged runtime support is not required in this slice.
- Bulk-first and token-efficient: responses return paths, counts, and component names, not full Blueprint graphs.
- No raw Blueprint graph editing in v1.
- Animation presets are reusable assets under `/Game`.
- Attachment supports placed actors selected by names/tags and Blueprint assets selected by `blueprint_path`.

## Defaults

- Blueprint parent class: `Actor`.
- Static mesh component transform defaults to identity.
- Light kind defaults to `point`.
- Light color defaults to warm white `[1.0, 0.82, 0.55, 1.0]`.
- Light intensity defaults to `5000.0`.
- LED parameter defaults to `EmissiveColor`.
- LED color defaults: off `[0.0, 0.0, 0.0, 1.0]`, on `[0.0, 0.85, 1.0, 1.0]`.
- Animation speed defaults to `1.0`.
- Moving light axis defaults to `[0.0, 0.0, 1.0]`.
- Moving light amplitude defaults to `100.0`.

## Testing

Rust tests cover protocol round trips, tool listing schemas, and representative MCP calls through the fake bridge. Unreal verification builds the plugin and runs live smoke calls against `MCPTester`: create a Blueprint actor, add mesh and light components, compile it, create LED and moving-light presets, spawn a test actor, attach presets by tag, query the actor, and clean up smoke actors.
