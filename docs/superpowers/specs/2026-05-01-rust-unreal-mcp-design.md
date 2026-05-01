# Rust Unreal MCP Design

Date: 2026-05-01

## Goal

Build a high-performance, token-efficient MCP system for controlling Unreal Editor 5.5+ from Codex. Version 1 targets editor automation only and focuses on bulk scene creation, game-ready level setup, materials, lighting, landscapes, Blueprints, and generated scene helpers.

## Non-Goals For Version 1

- Packaged game runtime control.
- Remote network control beyond localhost.
- Real-time multiplayer gameplay debugging.
- Replacing Unreal's editor UI.
- Full Blueprint graph parity with every editor feature.

These are intentionally deferred so the first version can be reliable, buildable, and fast.

## Scope

Version 1 includes:

- Connection and capability discovery.
- Level creation, opening, saving, duplication, listing, and world settings.
- Bulk actor spawn, delete, transform updates, property updates, material assignment, querying, and snapshots.
- Asset folder creation, listing, import, deletion, and bulk rename.
- Material creation, material instance creation, procedural texture creation, parameter updates, and bulk apply.
- Landscape creation, flattening, material application, and layer painting.
- Blueprint creation, component addition, variable setting, compilation, inspection, and batched graph edits.
- Lighting, sky, fog, post-process, night scenes, and time-of-day helpers.
- Scene helpers for generated game blockouts, cities, roads, buildings, and cleanup.
- Editor utilities for selection, focus, transactions, undo, and dry runs.

## Architecture

The system has two main deliverables.

### Rust MCP Server

The Rust server speaks MCP to Codex over stdio and exposes the public tool surface. It owns:

- Tool schemas and validation.
- Bulk request shaping.
- Response mode handling.
- Connection state with the Unreal bridge.
- Request retries where safe.
- Compact result summaries.
- Logs and debug traces.
- Integration tests using a fake Unreal bridge.

The Rust server does not directly mutate Unreal assets or editor state. It translates MCP tool calls into bridge commands and returns compact results to Codex.

### Unreal Editor Plugin

The Unreal side is a C++ editor plugin for UE 5.5+. It runs inside Unreal Editor and executes commands through Unreal editor APIs. It owns:

- Localhost bridge listener.
- Protocol decode and encode.
- Command dispatch.
- Unreal transactions.
- Asset and object lookup.
- Actor and asset handle resolution.
- Editor-safe execution on the game thread where needed.
- Unreal-side logging.

The plugin is editor-only for version 1. Runtime modules are avoided until packaged game support is explicitly added.

## Transport

Codex communicates with the Rust MCP server through normal MCP stdio. The Rust server communicates with Unreal Editor through localhost TCP.

Bridge transport choices:

- Address: `127.0.0.1`.
- Default payload format: compact binary MessagePack-compatible envelope.
- Debug payload format: JSON envelope for local inspection and tests.
- TLS and WSS are not used for local editor control.

This avoids the certificate failure path seen with WSS while keeping the bridge fast and easy to reconnect after editor/plugin reloads.

## Protocol Envelope

Every bridge request uses a batch envelope:

```text
RequestEnvelope
  protocol_version: u16
  request_id: u64
  response_mode: Summary | Handles | Full
  error_mode: Stop | Continue
  commands: Command[]
```

Every bridge response uses a matching batch envelope:

```text
ResponseEnvelope
  protocol_version: u16
  request_id: u64
  ok: bool
  elapsed_ms: u32
  results: CommandResult[]
  errors: IndexedError[]
```

`response_mode` controls token use:

- `Summary`: counts, elapsed time, warnings, and short labels only.
- `Handles`: summary plus stable handles and asset paths.
- `Full`: detailed objects for explicit inspection work.

`error_mode` controls partial failure:

- `Stop`: abort the batch after the first failed command.
- `Continue`: execute remaining commands and return indexed failures.

## Command Model

The public MCP tools are grouped by domain. Tools accept compact requests and prefer bulk operations.

### Connection

- `connection.ping`
- `connection.status`
- `connection.reconnect`
- `connection.capabilities`

### Levels

- `level.create`
- `level.open`
- `level.save`
- `level.duplicate`
- `level.list`
- `level.set_world_settings`

### World

- `world.query`
- `world.bulk_spawn`
- `world.bulk_delete`
- `world.bulk_update_transform`
- `world.bulk_set_properties`
- `world.bulk_set_materials`
- `world.snapshot`

### Assets

- `asset.list`
- `asset.import`
- `asset.create_folder`
- `asset.delete`
- `asset.bulk_rename`

### Materials

- `material.create`
- `material.create_instance`
- `material.create_procedural_texture`
- `material.bulk_apply`
- `material.set_parameters`

### Landscape

- `landscape.create`
- `landscape.flatten`
- `landscape.apply_material`
- `landscape.paint_layers`

### Blueprint

- `blueprint.create`
- `blueprint.add_components`
- `blueprint.set_variables`
- `blueprint.compile`
- `blueprint.inspect`
- `blueprint.graph.batch_edit`

### Lighting

- `lighting.set_time_of_day`
- `lighting.set_night_scene`
- `lighting.bulk_set_lights`
- `lighting.set_sky`
- `lighting.set_fog`
- `lighting.set_post_process`

### Scene Helpers

- `scene.clear_generated`
- `scene.build_city`
- `scene.build_roads`
- `scene.build_buildings`
- `scene.build_gameplay_blockout`

### Editor Utilities

- `editor.select`
- `editor.focus`
- `editor.transaction_begin`
- `editor.transaction_commit`
- `editor.undo`

## Handles And Tags

Generated objects receive stable metadata so later calls can target groups without resending large scene descriptions.

Required tags:

- `mcp.generated`
- `mcp.scene:<scene_name>`
- `mcp.group:<group_name>`
- `mcp.layer:<layer_name>`

Handles resolve to one of:

- Actor GUID.
- Actor path.
- Asset object path.
- Package path.
- Generated tag selector.

The Rust server treats handles as opaque strings. The Unreal plugin owns resolution.

## Bulk Data Strategy

The API must avoid one MCP call per actor or asset.

Bulk commands support:

- Arrays of actor spawn specs.
- Arrays of transform deltas.
- Arrays of property patches.
- Arrays of material assignments.
- Generated scene recipes that produce many objects from one request.
- `dry_run: true` for estimating counts and asset paths before writing.

Large read responses can be written to snapshot files. The MCP response then returns:

- Snapshot file path.
- Actor count.
- Checksum.
- Timestamp.

## Unreal Plugin Internals

The plugin structure is:

```text
unreal/UnrealMCPBridge
  UnrealMCPBridge.uplugin
  Source
    UnrealMCPBridge
      Public
      Private
        Bridge
        Protocol
        Commands
        Handles
        Logging
    ThirdParty
```

Command handlers are split by domain:

- `LevelCommands`
- `WorldCommands`
- `AssetCommands`
- `MaterialCommands`
- `LandscapeCommands`
- `BlueprintCommands`
- `LightingCommands`
- `SceneCommands`
- `EditorCommands`

Each handler receives a validated command payload and returns a compact command result. Handlers that mutate editor state run inside Unreal transactions unless the request explicitly disables transactions.

## Rust Workspace Structure

The Rust workspace structure is:

```text
crates
  unreal-mcp-protocol
  unreal-mcp-server
  unreal-mcp-tests
```

`unreal-mcp-protocol` owns:

- Request and response envelope types.
- Command enum.
- Result enum.
- Error types.
- Handle types.
- MessagePack encode/decode.
- JSON debug encode/decode.

`unreal-mcp-server` owns:

- MCP tool registration.
- MCP request validation.
- Bridge client.
- Connection lifecycle.
- Response shaping.
- Logging.

`unreal-mcp-tests` owns:

- Fake bridge server.
- End-to-end command tests.
- Protocol compatibility tests.

## Performance Requirements

Version 1 must follow these constraints:

- No per-actor round trips for bulk operations.
- Default responses use `Summary`.
- Full scene data is only returned when requested.
- The bridge caches repeated asset, material, class, and actor lookups within one request.
- Generated helpers return counts and handles instead of verbose per-object descriptions.
- Snapshot files are used for large read results.
- Command payloads are versioned so protocol changes can be made without ambiguous behavior.

## Error Handling

Errors are structured and indexed.

Each error includes:

- `command_index`
- Optional `item_index`
- Stable error code.
- Human-readable short message.
- Optional detail path or object path.

Examples:

- `BridgeUnavailable`
- `UnsupportedProtocolVersion`
- `UnknownCommand`
- `InvalidPayload`
- `AssetNotFound`
- `ActorNotFound`
- `UnrealApiFailure`
- `PartialFailure`

## Security Model

Version 1 is local editor automation only.

- The bridge listens on localhost.
- No remote bind address is enabled by default.
- No TLS is required for localhost.
- The bridge rejects unsupported protocol versions.
- File writes are limited to explicit Unreal project paths and explicit snapshot/log paths.
- Dangerous commands such as bulk delete require explicit selectors and return dry-run support.

## Testing Strategy

Rust tests:

- Protocol round-trip tests for binary payloads.
- Protocol round-trip tests for JSON debug payloads.
- Tool schema validation tests.
- Bulk request response-shaping tests.
- Fake bridge integration tests.

Unreal tests:

- Plugin load smoke test.
- Protocol decode/encode test.
- Level create/open/save smoke command.
- Bulk actor spawn smoke command.
- Material creation smoke command.
- Lighting setup smoke command.

End-to-end tests:

- Rust server connects to fake bridge.
- Rust server sends batched commands.
- Fake bridge returns indexed partial failures.
- Server returns compact MCP responses.

## Documentation

Required docs:

- `README.md`: project overview and quick start.
- `CONTRIBUTING.md`: commit norm, branch norm, formatting, and test expectations.
- `docs/architecture/overview.md`: architecture and data flow.
- `docs/protocol/envelope.md`: bridge envelope and versioning.
- `docs/protocol/tools.md`: public MCP tools.
- `docs/protocol/errors.md`: error model.
- `examples/city_scene.json`: generated city request.
- `examples/night_lighting.json`: night lighting request.

## Commit Norm

The project uses Conventional Commits.

Allowed commit types:

- `feat`: user-facing functionality.
- `fix`: bug fixes.
- `docs`: documentation.
- `test`: tests.
- `refactor`: behavior-preserving code changes.
- `perf`: performance improvements.
- `chore`: tooling, dependency, and maintenance changes.

Every commit should be scoped, buildable, and focused. Formatting-only churn must not be mixed with feature work.

## Milestones

### Milestone 1: Repo Foundation

- Add workspace structure.
- Add `CONTRIBUTING.md`.
- Add Rust formatting and lint baseline.
- Add protocol crate skeleton.
- Add initial tests.

### Milestone 2: Rust Protocol And MCP Server

- Implement protocol envelope.
- Implement binary and JSON debug encoding.
- Implement fake bridge.
- Implement connection tools.
- Implement compact response modes.

### Milestone 3: Unreal Plugin Skeleton

- Add UE 5.5+ editor plugin.
- Start localhost bridge.
- Decode and encode protocol envelopes.
- Add ping/status/capabilities.

### Milestone 4: Core Editor Commands

- Implement level commands.
- Implement world bulk spawn/delete/query/update.
- Implement material creation/application.
- Implement lighting setup.

### Milestone 5: Game Creation Helpers

- Implement landscape helpers.
- Implement Blueprint basics.
- Implement city, roads, buildings, and gameplay blockout helpers.
- Add examples and smoke tests.

## Approval Status

The user approved this design direction in conversation before implementation planning. The next step is an implementation plan using the Superpowers planning workflow.
