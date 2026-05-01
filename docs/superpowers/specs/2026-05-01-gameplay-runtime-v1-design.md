# Gameplay Runtime v1 Design

## Goal

Turn editor gameplay markers into Play-in-Editor-ready runtime actors by binding lightweight runtime components in bulk. This layer should make collectibles, checkpoints, interactions, and objective markers carry real runtime behavior without generating Blueprint graphs.

## Tool Set

- `gameplay.create_system`: create or update one gameplay manager actor.
- `gameplay.bind_collectibles`: add collectible runtime components to selected actors.
- `gameplay.bind_checkpoints`: add checkpoint runtime components to selected actors.
- `gameplay.bind_interactions`: add interaction runtime components to selected actors.
- `gameplay.bind_objective_flow`: add objective runtime components to selected actors.

## Runtime Classes

- `UUnrealMCPGameplayManagerComponent`: stores runtime score, active checkpoint, and objective progress.
- `UUnrealMCPCollectibleComponent`: listens for overlap, awards score to the manager, then hides or destroys the collectible actor.
- `UUnrealMCPCheckpointComponent`: listens for overlap and records the active checkpoint.
- `UUnrealMCPInteractionComponent`: listens for overlap and records an interaction; for `open` actions it hides/disables the named target actor.
- `UUnrealMCPObjectiveComponent`: listens for overlap and completes objectives in order through the manager.

The manager is attached to a normal `AActor` spawned by the bridge. Bound actors store the manager actor label, so runtime components can resolve the manager during `BeginPlay`.

## Protocol Shape

Each tool maps to one bridge command with a `spec` payload. Results use:

```text
gameplay_runtime_operation {
  manager,
  bindings,
  count,
  collectible_count,
  checkpoint_count,
  interaction_count,
  objective_count
}
```

`bindings` is a compact list of `{ name, path, component }`.

## Selection Rules

Binding tools accept `names`, `tags`, `manager_name`, and `include_generated`. If no names or tags are supplied, each binding tool uses the matching Gameplay Foundation tag:

- collectibles: `mcp.gameplay_actor:collectible`
- checkpoints: `mcp.gameplay_actor:checkpoint`
- interactions: `mcp.gameplay_actor:interaction`
- objectives: `mcp.gameplay_actor:objective`

Bridge code reads existing marker tags such as `mcp.value:*`, `mcp.checkpoint:*`, `mcp.interaction_id:*`, `mcp.objective:*`, and `mcp.order:*` to configure components.

## Non-Goals

- No HUD or UI.
- No persistent save system.
- No custom pawn/controller generation.
- No Blueprint graph generation.

