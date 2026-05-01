# Gameplay Foundation v1 Plan

## Phase 1: Protocol

- Add gameplay request specs and compact operation result to `unreal-mcp-protocol`.
- Add JSON and MessagePack round-trip tests for all five commands and the result shape.
- Export the new protocol types.

## Phase 2: MCP Server

- Add five MCP tool schemas with top-level object schemas only.
- Dispatch each tool into its protocol command.
- Return compact operation summaries and JSON data.
- Extend fake bridge coverage and stdio tool-list/call tests.

## Phase 3: Unreal Bridge

- Add the five command names to bridge capabilities.
- Implement editor handlers for player starts, checkpoint markers, interaction markers, collectible grids, and objective flows.
- Reuse existing actor spawning/tagging patterns and return `gameplay_operation`.

## Phase 4: Docs And Verification

- Document the tools in `docs/protocol/tools.md`.
- Run Rust fmt/test/clippy.
- Build the Unreal plugin and the MCPTester editor target.
- Replace the local Codex MCP server binary and smoke-test the MCP tool list.

