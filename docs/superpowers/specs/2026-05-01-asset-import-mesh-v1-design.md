# Asset Import Mesh v1 Design

## Goal

Add editor-only MCP support for importing local assets and generating reusable mesh assets, so city scenes can move beyond gray primitive blocks without requiring manual Content Browser work.

## Scope

This slice adds seven high-value tools:

- `asset.import_texture`: import one local texture file into `/Game/...`.
- `asset.import_static_mesh`: import one local static mesh file into `/Game/...`.
- `asset.bulk_import`: import many textures/static meshes in one MCP call.
- `asset.validate`: validate many Unreal asset paths and return compact existence/class data.
- `mesh.create_building`: generate a reusable static mesh building asset with simple facade geometry.
- `mesh.create_sign`: generate a reusable static mesh sign asset.
- `static_mesh.set_collision`: update simple collision settings for static mesh assets.

Automatic road mesh generation, FBX scene hierarchy import, complex material graph authoring, and Nanite/HLOD generation are deferred. They build cleanly on top of reliable asset creation and validation.

## Architecture

The Rust protocol gets asset import, validation, generated mesh, and static mesh operation command/result structs. The MCP stdio layer exposes object-only JSON schemas, validates argument shapes through serde, and dispatches one compact bridge request per tool call.

The Unreal bridge owns all editor operations in `BridgeServer.cpp`, matching the existing bulk-first pattern. Asset import uses editor asset tools with automated import data. Mesh generation creates `UStaticMesh` assets from `FMeshDescription`, so results are reusable Content Browser assets rather than transient actors. Static mesh operations edit existing assets and return compact per-asset summaries.

## Data Model

`AssetImportSpec` carries source path, destination path, asset type, replace behavior, and optional import options. `AssetValidateSpec` carries asset paths. `GeneratedBuildingSpec` carries destination path, dimensions, floor count, facade options, and tags. `GeneratedSignSpec` carries destination path, dimensions, and optional text metadata. `StaticMeshCollisionSpec` carries asset paths and collision mode. Result payloads return only paths, class names, success flags, and short messages.

## Error Handling

Rust keeps MCP schemas free of top-level `anyOf`, `oneOf`, `allOf`, and `not`. The bridge returns structured errors for missing source files, invalid `/Game/...` destinations, unsupported import types, import failures, invalid mesh dimensions, missing assets, and non-static-mesh asset targets. Bulk operations preserve item order and report per-item success instead of aborting the entire batch when one asset fails.

## Testing

Protocol tests cover request/result round-trips. MCP stdio tests cover tool listing, schema shape, and representative dispatch through the fake bridge. Unreal verification compiles the plugin with UE 5.7, builds `MCPTesterEditor`, and runs a live smoke test that imports a generated PNG texture, creates building and sign mesh assets, validates them, updates mesh collision, spawns a generated mesh, and cleans up the actor.
