# Gameplay Runtime v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add MCP tools and Unreal runtime components that bind playable collectible, checkpoint, interaction, and objective behavior to generated gameplay markers.

**Architecture:** Rust protocol/server changes expose five compact `gameplay.*` tools. The Unreal bridge creates a manager actor and attaches runtime components to selected actors in bulk. Runtime components use overlap events and manager actor labels, avoiding Blueprint graph generation.

**Tech Stack:** Rust, serde, Tokio, MCP stdio JSON-RPC, Unreal Engine 5.7 C++ plugin, `UActorComponent`, editor actor/component APIs.

---

### Task 1: Protocol Types

**Files:**
- Modify: `crates/unreal-mcp-protocol/src/commands.rs`
- Modify: `crates/unreal-mcp-protocol/src/lib.rs`
- Test: `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] Write failing request/result round-trip tests for `gameplay.create_system`, `gameplay.bind_collectibles`, `gameplay.bind_checkpoints`, `gameplay.bind_interactions`, and `gameplay.bind_objective_flow`.
- [ ] Run `cargo test -p unreal-mcp-protocol gameplay_runtime --tests` and verify unresolved type/variant failures.
- [ ] Add specs, binding summary structs, `GameplayRuntimeOperationResult`, and command/result variants.
- [ ] Export the new types from `lib.rs`.
- [ ] Run the focused protocol tests and commit with `feat(protocol): add gameplay runtime commands`.

### Task 2: MCP Server Tools

**Files:**
- Modify: `crates/unreal-mcp-server/src/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/src/tools.rs`
- Modify: `crates/unreal-mcp-server/tests/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/connection_tools.rs`
- Modify: `crates/unreal-mcp-tests/src/lib.rs`

- [ ] Add failing stdio tool-list and dispatch tests for the five new tools.
- [ ] Run the focused server tests and verify they fail because the tools are missing.
- [ ] Add object-only input schemas and dispatch handlers.
- [ ] Add argument structs, command conversion, result extraction, and compact response data.
- [ ] Extend fake bridge capabilities to 57 commands and fake runtime operation responses.
- [ ] Run focused server tests and commit with `feat(server): add gameplay runtime tools`.

### Task 3: Unreal Runtime Components

**Files:**
- Create: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/Runtime/UnrealMCPGameplayRuntimeComponents.h`
- Create: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Runtime/UnrealMCPGameplayRuntimeComponents.cpp`

- [ ] Add manager, collectible, checkpoint, interaction, and objective component classes.
- [ ] Implement `BeginPlay` overlap binding for primitive owner components.
- [ ] Implement manager score/checkpoint/objective methods.
- [ ] Implement simple interaction target hiding for `open` actions.

### Task 4: Unreal Bridge Commands

**Files:**
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`

- [ ] Include the gameplay runtime component header.
- [ ] Add five command names to capabilities.
- [ ] Add helpers to select actors, parse marker tags, attach or update components, and return `gameplay_runtime_operation`.
- [ ] Implement handlers for `gameplay_create_system`, `gameplay_bind_collectibles`, `gameplay_bind_checkpoints`, `gameplay_bind_interactions`, and `gameplay_bind_objective_flow`.
- [ ] Run UE `BuildPlugin` and commit with `feat(unreal): add gameplay runtime bridge commands`.

### Task 5: Docs And Verification

**Files:**
- Modify: `docs/protocol/tools.md`

- [ ] Document the five new `gameplay.*` tools and result shape.
- [ ] Run `cargo fmt --check`, `cargo test`, and `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Build the Unreal plugin with UE 5.7.
- [ ] Sync the plugin into `MCPTester`, build `MCPTesterEditor`, and run a live MCP smoke that creates markers, creates the runtime system, binds all four gameplay component types, queries component tags, and cleans up.
- [ ] Merge to main, replace `target-codex`, verify 57 tools, push, and remove the worktree.

