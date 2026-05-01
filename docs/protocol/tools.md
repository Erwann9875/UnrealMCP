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

## MCP Methods

The stdio server currently handles:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

Other MCP features, including resources, prompts, roots, sampling, logging, and
cancellation, are out of scope for this slice.
