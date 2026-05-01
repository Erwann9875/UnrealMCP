# Gameplay Foundation v1 Design

## Goal

Add editor-only MCP tools that place gameplay primitives into an Unreal level in bulk-friendly, token-efficient payloads. The tools should create useful authored markers without requiring runtime gameplay C++ classes yet.

## Tool Set

- `game.create_player`: create a player start marker and optional camera marker.
- `game.create_checkpoint`: create one checkpoint marker with id/order metadata.
- `game.create_interaction`: create one interactable trigger marker with kind/action metadata.
- `game.create_collectibles`: create many collectible markers in one request.
- `game.create_objective_flow`: create ordered objective step markers in one request.

## Protocol Shape

Each tool maps to a single protocol command with a `spec` payload. Results use one compact tagged result:

```text
gameplay_operation {
  spawned,
  count,
  player_count,
  checkpoint_count,
  interaction_count,
  collectible_count,
  objective_count
}
```

`spawned` reuses the existing compact `{ name, path }` actor summary.

## Editor Behavior

The bridge creates or replaces named editor actors using built-in Unreal types and `/Engine/BasicShapes/Cube.Cube` for visible markers where a specialized actor is not needed. It tags everything with:

- `mcp.generated`
- `mcp.gameplay`
- `mcp.gameplay_actor:<kind>`
- optional `mcp.scene:<scene>`
- optional `mcp.group:<group>`
- tool-specific ids such as `mcp.checkpoint:<id>`, `mcp.interaction:<kind>`, and `mcp.objective:<id>`

## Non-Goals

- No runtime playable pawn/controller implementation.
- No custom Blueprint graph generation.
- No game save/objective state machine yet.

