# Level And World Bulk Ops Design

## Purpose

This slice turns the bridge from a connection probe into a useful editor automation surface. It adds level management and bulk actor operations so later material, lighting, landscape, and scene-generation tools can work on real Unreal levels.

## Scope

Add MCP tools and bridge commands for:

- `level.create`
- `level.open`
- `level.save`
- `level.list`
- `world.bulk_spawn`
- `world.bulk_delete`
- `world.query`
- `world.snapshot`

`world.bulk_spawn` supports static mesh actors first. Each spawned actor gets metadata tags for generated-object cleanup: `mcp.generated`, `mcp.scene:<scene>`, and optional `mcp.group:<group>`.

## Data Shape

Requests stay compact and bulk-first. Actor spawn specs include:

- `name`
- `mesh`
- `location`
- `rotation`
- `scale`
- `scene`
- `group`

Responses default to summaries: counts, names, paths, elapsed time, and snapshot path where relevant. Full actor payloads are only returned by explicit query/snapshot tools.

## Unreal Execution

The Rust server still talks to Unreal through the existing length-prefixed JSON bridge. Unreal command handlers run editor mutations on the game thread, use editor transactions for writes, and avoid per-actor round trips.

## Testing

Rust tests cover protocol JSON/MessagePack round trips, fake-bridge command behavior, and MCP stdio tool dispatch. Unreal is verified by compiling the plugin and running live stdio smoke calls against `MCPTester`.
