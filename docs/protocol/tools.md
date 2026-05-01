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

## MCP Methods

The stdio server currently handles:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

Other MCP features, including resources, prompts, roots, sampling, logging, and
cancellation, are out of scope for this slice.
