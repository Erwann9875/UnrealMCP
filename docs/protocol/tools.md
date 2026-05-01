# MCP Tools

This foundation exposes the initial `connection` tool surface over minimal MCP
stdio JSON-RPC dispatch. It supports `initialize`, `tools/list`, and
`tools/call` for the tools below.

The Rust server connects to the Unreal bridge at `UNREAL_MCP_BRIDGE_ADDR`, or
`127.0.0.1:55557` when the environment variable is not set.

The bridge body format is selected with `UNREAL_MCP_BRIDGE_FORMAT`. Use
`json` for the current Unreal plugin bootstrap. `msgpack` remains the default
for the optimized bridge path and Rust fake-bridge tests.

## `connection.ping`

Checks whether the Unreal bridge responds.

Default response shape:

```json
{
  "ok": true,
  "bridge_version": "0.1.0",
  "elapsed_ms": 1
}
```

## `connection.status`

Returns compact connection and Unreal version details.

Default response data shape:

```json
{
  "connected": true,
  "bridge_version": "0.1.0",
  "unreal_version": "5.5.0",
  "elapsed_ms": 1
}
```

## `connection.capabilities`

Returns supported command names.

Default response data shape:

```json
{
  "commands": [
    "connection.ping",
    "connection.status",
    "connection.capabilities"
  ],
  "elapsed_ms": 1
}
```

## `level.create`

Creates a blank editor level. Request arguments:

```json
{
  "path": "/Game/MCP/Generated/L_Test",
  "open": true,
  "save": true
}
```

## `level.open`

Opens an existing level asset.

```json
{
  "path": "/Game/FirstPerson/Lvl_FirstPerson"
}
```

## `level.save`

Saves the current level, or a specified level path when `path` is present.

```json
{
  "path": "/Game/MCP/Generated/L_Test"
}
```

## `level.list`

Lists project level assets with compact `{ path, name }` entries.

## `world.bulk_spawn`

Spawns static mesh actors in one bridge request. Each actor is tagged with
`mcp.generated`, plus optional `mcp.scene:<scene>` and `mcp.group:<group>`.

```json
{
  "actors": [
    {
      "name": "MCP_Test_Cube",
      "mesh": "/Engine/BasicShapes/Cube.Cube",
      "location": [0.0, 0.0, 50.0],
      "rotation": [0.0, 0.0, 0.0],
      "scale": [1.0, 1.0, 1.0],
      "scene": "test_scene",
      "group": "test_group"
    }
  ]
}
```

## `world.bulk_delete`

Deletes actors by label/name or tag. Empty selectors are rejected.

```json
{
  "names": ["MCP_Test_Cube"],
  "tags": []
}
```

## `world.query`

Returns matching actor names, paths, classes, transforms, and tags. Use
`include_generated: true` to query `mcp.generated` actors without passing tags.

```json
{
  "tags": ["mcp.generated"],
  "include_generated": true,
  "limit": 100
}
```

## `world.snapshot`

Writes a world actor snapshot JSON file and returns its path plus actor count.

```json
{
  "path": "Saved/UnrealMCP/snapshots/world_snapshot.json",
  "tags": ["mcp.generated"]
}
```

## `asset.create_folder`

Creates a Content Browser folder path.

```json
{
  "path": "/Game/MCP/Materials"
}
```

## `asset.import_texture`

Imports one local texture file into a Content Browser asset. `srgb` is applied
after import when present.

```json
{
  "source_file": "C:/Temp/asphalt.png",
  "destination_path": "/Game/MCP/Textures/T_Asphalt",
  "replace_existing": true,
  "save": false,
  "srgb": true
}
```

## `asset.import_static_mesh`

Imports one local static mesh file into a Content Browser asset. Generated
simple collision can be enabled with `generate_collision`.

```json
{
  "source_file": "C:/Temp/tower.fbx",
  "destination_path": "/Game/MCP/Meshes/SM_Tower",
  "replace_existing": true,
  "save": false,
  "generate_collision": true
}
```

## `asset.bulk_import`

Imports many texture/static mesh files in one bridge request. Each item returns
`source_file`, `path`, `class_name`, `imported`, and optional `message`.

```json
{
  "items": [
    {
      "kind": "texture",
      "source_file": "C:/Temp/asphalt.png",
      "destination_path": "/Game/MCP/Textures/T_Asphalt",
      "srgb": true
    },
    {
      "kind": "static_mesh",
      "source_file": "C:/Temp/tower.fbx",
      "destination_path": "/Game/MCP/Meshes/SM_Tower",
      "generate_collision": true
    }
  ]
}
```

## `asset.validate`

Validates asset paths and returns compact existence/class data.

```json
{
  "paths": [
    "/Game/MCP/Textures/T_Asphalt",
    "/Game/MCP/Meshes/SM_Tower"
  ]
}
```

## `mesh.create_building`

Creates or updates a reusable generated `UStaticMesh` building asset. The mesh
uses a cuboid body plus simple raised window panels, and its pivot is at the
base center so spawned actors sit on the ground.

```json
{
  "path": "/Game/MCP/Meshes/SM_DTLA_Tower_A",
  "width": 800.0,
  "depth": 600.0,
  "height": 2400.0,
  "floors": 12,
  "window_rows": 12,
  "window_columns": 6,
  "material": "/Game/MCP/Materials/M_Glass"
}
```

## `mesh.create_sign`

Creates or updates a reusable generated sign panel static mesh.

```json
{
  "path": "/Game/MCP/Meshes/SM_HollywoodSignPanel",
  "width": 900.0,
  "height": 240.0,
  "depth": 30.0,
  "text": "HOLLYWOOD",
  "material": "/Game/MCP/Materials/M_WhitePaint"
}
```

## `static_mesh.set_collision`

Sets collision trace mode and optional simple box collision for many static
mesh assets in one request.

```json
{
  "paths": ["/Game/MCP/Meshes/SM_DTLA_Tower_A"],
  "collision_trace": "use_simple_as_complex",
  "simple_collision": true,
  "rebuild": true
}
```

## `road.create_network`

Creates a grid road network from static mesh actors in one bridge request.
Road actors are tagged with `mcp.scene_actor:road` plus optional scene/group
tags.

```json
{
  "name_prefix": "MCP_DTLA_Road",
  "scene": "la",
  "group": "downtown_roads",
  "origin": [0.0, 0.0, 0.0],
  "rows": 3,
  "columns": 4,
  "block_size": [2400.0, 1800.0],
  "road_width": 320.0,
  "road_thickness": 20.0,
  "road_mesh": "/Engine/BasicShapes/Cube.Cube"
}
```

## `scene.bulk_place_on_grid`

Places repeated static mesh actors on a deterministic centered grid. Use this
for lots, props, towers, lamps, or other repeated scene parts without sending
one spawn entry per actor.

```json
{
  "name_prefix": "MCP_DTLA_Tower",
  "mesh": "/Game/MCP/Meshes/SM_DTLA_Tower_A",
  "scene": "la",
  "group": "downtown_buildings",
  "origin": [0.0, 0.0, 0.0],
  "rows": 4,
  "columns": 5,
  "spacing": [600.0, 650.0],
  "rotation": [0.0, 0.0, 0.0],
  "scale": [1.0, 1.0, 1.0],
  "yaw_variation": 12.0,
  "scale_variation": 0.2,
  "seed": 42
}
```

## `scene.create_city_block`

Creates one city block with four roads, optional sidewalks, and a building
grid. Roads and sidewalks default to `/Engine/BasicShapes/Cube.Cube`; buildings
use the supplied mesh.

```json
{
  "name_prefix": "MCP_DTLA_Block_A",
  "scene": "la",
  "group": "downtown_block_a",
  "origin": [0.0, 0.0, 0.0],
  "size": [2400.0, 1800.0],
  "road_width": 320.0,
  "sidewalk_width": 180.0,
  "building_mesh": "/Game/MCP/Meshes/SM_DTLA_Tower_A",
  "building_rows": 2,
  "building_columns": 3,
  "building_scale": [2.0, 2.0, 8.0],
  "seed": 7
}
```

## `scene.create_district`

Creates a preset multi-block district with a road network and deterministic
building placement. Supported presets are `downtown`, `residential`,
`industrial`, `beach`, and `hills`.

```json
{
  "name_prefix": "MCP_DTLA",
  "preset": "downtown",
  "scene": "la",
  "group": "downtown",
  "origin": [0.0, 0.0, 0.0],
  "blocks": [3, 2],
  "block_size": [2400.0, 1800.0],
  "road_width": 320.0,
  "building_mesh": "/Game/MCP/Meshes/SM_DTLA_Tower_A",
  "seed": 99
}
```

## `material.create`

Creates a simple Unreal material asset with parameter nodes wired to common PBR
inputs.

```json
{
  "path": "/Game/MCP/Materials/M_Concrete",
  "base_color": [0.45, 0.45, 0.42, 1.0],
  "metallic": 0.0,
  "roughness": 0.8,
  "specular": 0.4,
  "emissive_color": [0.0, 0.0, 0.0, 1.0]
}
```

## `material.create_instance`

Creates a material instance from a parent material and applies optional
parameter overrides.

```json
{
  "path": "/Game/MCP/Materials/MI_Concrete_Dark",
  "parent": "/Game/MCP/Materials/M_Concrete",
  "scalar_parameters": [
    { "name": "Roughness", "value": 0.9 }
  ],
  "vector_parameters": [
    { "name": "BaseColor", "value": [0.25, 0.25, 0.25, 1.0] }
  ],
  "texture_parameters": []
}
```

## `material.create_procedural_texture`

Creates a small `solid` or `checker` texture asset.

```json
{
  "path": "/Game/MCP/Materials/T_Asphalt_Checker",
  "pattern": "checker",
  "width": 128,
  "height": 128,
  "color_a": [0.05, 0.05, 0.05, 1.0],
  "color_b": [0.12, 0.12, 0.12, 1.0],
  "checker_size": 8
}
```

## `material.set_parameters`

Updates scalar, vector, and texture parameters on a material instance.

```json
{
  "path": "/Game/MCP/Materials/MI_Concrete_Dark",
  "scalar_parameters": [
    { "name": "Roughness", "value": 0.7 }
  ],
  "vector_parameters": [],
  "texture_parameters": []
}
```

## `material.bulk_apply`

Applies one or more material assignments to actors selected by names or tags.
Each assignment targets one material and one material slot.

```json
{
  "assignments": [
    {
      "material": "/Game/MCP/Materials/M_Concrete",
      "tags": ["mcp.group:buildings"],
      "slot": 0
    }
  ]
}
```

## `world.bulk_set_materials`

Convenience wrapper for applying one material to actors selected by names or
tags.

```json
{
  "material": "/Game/MCP/Materials/M_Concrete",
  "tags": ["mcp.group:buildings"],
  "slot": 0
}
```

## `lighting.set_night_scene`

Configures a reusable night preset: one active moon directional light, sky
light, sky atmosphere, fog, and unbound post-process exposure. Other
directional lights are hidden and set to intensity `0` to avoid forward-shading
priority warnings.

```json
{
  "moon_rotation": [-35.0, -25.0, 0.0],
  "moon_intensity": 0.12,
  "moon_color": [0.55, 0.65, 1.0, 1.0],
  "sky_intensity": 0.05,
  "fog_density": 0.01,
  "exposure_compensation": -0.5
}
```

## `lighting.set_sky`

Creates or updates `MCP_SkyLight` and `MCP_SkyAtmosphere`.

```json
{
  "sky_intensity": 0.05,
  "lower_hemisphere_color": [0.01, 0.012, 0.018, 1.0]
}
```

## `lighting.set_fog`

Creates or updates `MCP_NightFog`.

```json
{
  "density": 0.01,
  "height_falloff": 0.2,
  "color": [0.08, 0.1, 0.16, 1.0],
  "start_distance": 0.0
}
```

## `lighting.set_post_process`

Creates or updates an unbound `MCP_PostProcess` volume for exposure and bloom.

```json
{
  "exposure_compensation": -0.5,
  "min_brightness": 0.2,
  "max_brightness": 1.0,
  "bloom_intensity": 0.6
}
```

## `lighting.bulk_set_lights`

Creates or updates point, rect, and spot lights in one request. Each light is
tagged with `mcp.generated` and `mcp.lighting`, plus optional custom tags.

```json
{
  "lights": [
    {
      "name": "MCP_StreetLight_001",
      "kind": "point",
      "location": [100.0, 200.0, 360.0],
      "rotation": [0.0, 0.0, 0.0],
      "scale": [1.0, 1.0, 1.0],
      "color": [1.0, 0.82, 0.55, 1.0],
      "intensity": 5000.0,
      "attenuation_radius": 1000.0,
      "source_radius": 24.0,
      "tags": ["mcp.scene:lighting_smoke"]
    },
    {
      "name": "MCP_WindowGlow_001",
      "kind": "rect",
      "location": [140.0, 200.0, 500.0],
      "source_width": 240.0,
      "source_height": 160.0
    }
  ]
}
```

## `lighting.set_time_of_day`

Configures one active sun directional light and disables other directional
lights.

```json
{
  "sun_rotation": [-10.0, 110.0, 0.0],
  "sun_intensity": 1.0,
  "sun_color": [1.0, 0.93, 0.82, 1.0]
}
```

## `landscape.create`

Creates or updates a named Unreal Landscape actor. Component count defaults to
`[4, 4]`, section size defaults to `63`, sections per component defaults to
`1`, and scale defaults to `[100.0, 100.0, 100.0]`.

```json
{
  "name": "MCP_Landscape",
  "component_count": [4, 4],
  "section_size": 63,
  "sections_per_component": 1,
  "location": [0.0, 0.0, 0.0],
  "scale": [100.0, 100.0, 100.0],
  "material": "/Game/MCP/Materials/M_Ground",
  "tags": ["mcp.scene:la"]
}
```

Returns a compact landscape operation with `name`, `path`, `component_count`,
`vertex_count`, and changed fields.

## `landscape.set_heightfield`

Updates an existing landscape heightfield in one bridge command. If `samples`
is empty, the bridge generates a procedural heightfield from `amplitude`,
`frequency`, `seed`, and optional center flattening via `city_pad_radius`.
If `samples` is provided, it must contain exactly `width * height` normalized
height values.

```json
{
  "name": "MCP_Landscape",
  "amplitude": 1200.0,
  "frequency": 2.0,
  "seed": 1234,
  "city_pad_radius": 6500.0,
  "city_pad_falloff": 1500.0
}
```

## `landscape.paint_layers`

Assigns a landscape material and registers named paint layers. The first layer
is filled to full weight and later layers are initialized to zero weight.

```json
{
  "name": "MCP_Landscape",
  "material": "/Game/MCP/Materials/M_Ground",
  "layers": ["Concrete", "Hills", "Sand"]
}
```

## `placement.bulk_snap_to_ground`

Snaps actors selected by names or tags onto the first blocking `WorldStatic`
surface found by a vertical trace. `include_generated` selects actors tagged
with `mcp.generated` when no explicit tags are supplied.

```json
{
  "tags": ["mcp.group:buildings"],
  "include_generated": true,
  "trace_distance": 50000.0,
  "offset_z": 0.0
}
```

Returns snapped actor summaries with old and new locations.

## `blueprint.create_actor`

Creates an actor Blueprint asset. `parent_class` defaults to `Actor`.

```json
{
  "path": "/Game/MCP/Blueprints/BP_LED_Tower",
  "parent_class": "Actor"
}
```

## `blueprint.add_static_mesh_component`

Adds or updates a static mesh component template on an actor Blueprint.

```json
{
  "blueprint": "/Game/MCP/Blueprints/BP_LED_Tower",
  "name": "BuildingMesh",
  "mesh": "/Engine/BasicShapes/Cube.Cube",
  "material": "/Game/MCP/Materials/MI_LED",
  "location": [0.0, 0.0, 200.0],
  "rotation": [0.0, 0.0, 0.0],
  "scale": [4.0, 4.0, 8.0]
}
```

## `blueprint.add_light_component`

Adds or updates a point, rect, or spot light component template on an actor
Blueprint.

```json
{
  "blueprint": "/Game/MCP/Blueprints/BP_LED_Tower",
  "name": "WindowGlow",
  "kind": "rect",
  "location": [0.0, -210.0, 500.0],
  "rotation": [0.0, 180.0, 0.0],
  "color": [0.0, 0.85, 1.0, 1.0],
  "intensity": 8000.0,
  "attenuation_radius": 1200.0,
  "source_width": 320.0,
  "source_height": 160.0
}
```

## `blueprint.compile`

Compiles a Blueprint asset.

```json
{
  "path": "/Game/MCP/Blueprints/BP_LED_Tower",
  "save": false
}
```

## `runtime.create_led_animation`

Creates a reusable runtime animation preset asset for pulsing a vector material
parameter, usually `EmissiveColor`.

```json
{
  "path": "/Game/MCP/Runtime/DA_LED_Pulse",
  "target_component": "BuildingMesh",
  "parameter_name": "EmissiveColor",
  "color_a": [0.0, 0.0, 0.0, 1.0],
  "color_b": [0.0, 0.85, 1.0, 1.0],
  "from_scalar": 0.0,
  "to_scalar": 10.0,
  "speed": 2.0
}
```

## `runtime.create_moving_light_animation`

Creates a reusable runtime animation preset for moving a light component and
pulsing its intensity/color.

```json
{
  "path": "/Game/MCP/Runtime/DA_WindowGlow_Move",
  "target_component": "WindowGlow",
  "axis": [0.0, 0.0, 1.0],
  "amplitude": 75.0,
  "speed": 1.5,
  "base_intensity": 8000.0
}
```

## `runtime.create_material_parameter_animation`

Creates a reusable runtime animation preset for pulsing a scalar material
parameter.

```json
{
  "path": "/Game/MCP/Runtime/DA_GlowAmount_Pulse",
  "target_component": "BuildingMesh",
  "parameter_name": "GlowAmount",
  "from_scalar": 0.0,
  "to_scalar": 5.0,
  "speed": 3.0
}
```

## `runtime.attach_animation_to_actor`

Attaches animation preset assets to placed actors selected by names/tags and/or
to an actor Blueprint. The bridge adds an `MCP_RuntimeAnimator` component that
ticks during Play-in-Editor.

```json
{
  "names": ["MCP_LED_Tower_001"],
  "tags": ["mcp.group:buildings"],
  "blueprint": "/Game/MCP/Blueprints/BP_LED_Tower",
  "animations": [
    "/Game/MCP/Runtime/DA_LED_Pulse",
    "/Game/MCP/Runtime/DA_WindowGlow_Move"
  ]
}
```

## `game.create_player`

Creates an editor `PlayerStart` plus an optional camera marker. Actors are
tagged with `mcp.generated`, `mcp.gameplay`, and `mcp.gameplay_actor:*`.

```json
{
  "name": "MCP_PlayerStart",
  "scene": "prototype",
  "group": "gameplay",
  "location": [0.0, 0.0, 120.0],
  "rotation": [0.0, 90.0, 0.0],
  "spawn_tag": "default",
  "create_camera": true
}
```

## `game.create_checkpoint`

Creates a visible checkpoint marker using the default cube mesh and tags it
with checkpoint id and order metadata.

```json
{
  "name": "MCP_Checkpoint_A",
  "checkpoint_id": "cp_a",
  "order": 1,
  "location": [1000.0, 0.0, 80.0],
  "scale": [2.0, 2.0, 0.25]
}
```

## `game.create_interaction`

Creates a visible interaction marker with kind, optional target/action/prompt
metadata, and selection-friendly gameplay tags.

```json
{
  "name": "MCP_DoorSwitch",
  "kind": "button",
  "interaction_id": "door_switch",
  "target": "MCP_Door_A",
  "action": "open",
  "prompt": "Open",
  "location": [500.0, 300.0, 120.0]
}
```

## `game.create_collectibles`

Creates many collectible markers in one bridge request. The mesh can be any
loaded static mesh asset path.

```json
{
  "name_prefix": "MCP_Coin",
  "mesh": "/Engine/BasicShapes/Cube.Cube",
  "scene": "prototype",
  "rows": 2,
  "columns": 3,
  "spacing": [180.0, 160.0],
  "value": 10,
  "animation": "/Game/MCP/Runtime/DA_CoinBob"
}
```

## `game.create_objective_flow`

Creates ordered objective markers from compact step data.

```json
{
  "name_prefix": "MCP_Objective",
  "steps": [
    {
      "id": "intro",
      "label": "Reach the street",
      "kind": "location",
      "location": [0.0, 0.0, 100.0]
    },
    {
      "id": "exit",
      "label": "Find the exit",
      "kind": "interaction",
      "location": [1200.0, 0.0, 100.0]
    }
  ]
}
```

Gameplay tools return the compact `gameplay_operation` data shape:

```json
{
  "count": 1,
  "player_count": 0,
  "checkpoint_count": 1,
  "interaction_count": 0,
  "collectible_count": 0,
  "objective_count": 0,
  "spawned": [
    {
      "name": "MCP_Checkpoint_A",
      "path": "PersistentLevel.MCP_Checkpoint_A"
    }
  ],
  "elapsed_ms": 1
}
```

## MCP Methods

The stdio server currently handles:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

Other MCP features, including resources, prompts, roots, sampling, logging, and
cancellation, are out of scope for this slice.
