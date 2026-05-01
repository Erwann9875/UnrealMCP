# Materials V1 Design

## Goal

Add editor-only material and texture tools so generated Unreal scenes can create reusable material assets and apply them to many spawned actors in bulk.

## Scope

This slice adds a compact material surface:

- `asset.create_folder`
- `material.create`
- `material.create_instance`
- `material.create_procedural_texture`
- `material.bulk_apply`
- `material.set_parameters`
- `world.bulk_set_materials`

`material.create` creates a simple physically based material asset from scalar/vector parameters. `material.create_instance` creates a material instance from a parent material. `material.create_procedural_texture` creates a small texture asset from a solid color or checker pattern. `material.bulk_apply` and `world.bulk_set_materials` assign material assets to actors selected by names or tags.

## Architecture

The Rust protocol gains material and asset commands/results. The MCP stdio layer exposes Codex-compatible object schemas without top-level schema composition keywords. The Unreal editor bridge performs all asset and actor mutations on the game thread, uses editor asset APIs for package creation, and returns compact summaries with counts and asset paths.

## Constraints

- Editor-only, Unreal Engine 5.5+.
- Bulk-first: one call must handle many material assignments.
- Token-efficient responses: summaries, counts, and asset paths; no large material graph dumps.
- Runtime LED animation is not included here; this slice only creates/apply materials and texture assets.
- Tool schemas must stay compatible with Codex function parameter validation.

## Testing

Rust tests cover protocol round trips, tool listing schemas, and representative MCP calls through the fake bridge. Unreal verification builds the plugin and runs live smoke calls against `MCPTester`: create folder, create material, spawn actor, apply material by tag, query actor, and clean up.
