# Landscape Grounding v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editor MCP tools for real Unreal landscapes and actor grounding.

**Architecture:** Extend the Rust protocol/server with four compact bulk-oriented commands, then implement the editor behavior in the Unreal bridge. Keep all MCP schemas as plain object schemas and keep bridge responses compact.

**Tech Stack:** Rust, serde, MCP stdio JSON-RPC, UE 5.7 C++ editor plugin, Landscape module.

---

### Task 1: Protocol Types

**Files:**
- Modify: `crates/unreal-mcp-protocol/src/commands.rs`
- Test: `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] Add failing round-trip tests for `LandscapeCreate`, `LandscapeSetHeightfield`, `LandscapePaintLayers`, and `PlacementBulkSnapToGround` plus their result payloads.
- [ ] Add `LandscapeCreateSpec`, `LandscapeHeightPatch`, `LandscapeLayerPaint`, `PlacementSnapSpec`, `LandscapeOperation`, `PlacementSnapActor`, and `PlacementSnapResult`.
- [ ] Add command and result enum variants.
- [ ] Run `cargo test -p unreal-mcp-protocol --test codec_roundtrip`.
- [ ] Commit with `feat(protocol): add landscape grounding commands`.

### Task 2: MCP Server Tools

**Files:**
- Modify: `crates/unreal-mcp-server/src/tools.rs`
- Modify: `crates/unreal-mcp-server/src/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/connection_tools.rs`
- Modify: `crates/unreal-mcp-tests/src/lib.rs`

- [ ] Add failing MCP tests for tool listing, schema required fields, and representative calls.
- [ ] Implement argument structs, defaults, command dispatch, result extraction, and compact summaries.
- [ ] Update fake bridge capabilities and command handlers.
- [ ] Run `cargo test -p unreal-mcp-server --test mcp_stdio` and `cargo test -p unreal-mcp-server --test connection_tools connection_tools_return_compact_capabilities_response`.
- [ ] Commit with `feat(server): add landscape grounding tools`.

### Task 3: Unreal Bridge

**Files:**
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`

- [ ] Add `Landscape` as a private dependency.
- [ ] Add helpers for finding named landscapes, creating landscape height data, procedural sample generation, layer info creation, and actor ground tracing.
- [ ] Add bridge capabilities and handlers for the four commands.
- [ ] Run UE `RunUAT BuildPlugin` with a short package path.
- [ ] Commit with `feat(unreal): add landscape grounding bridge commands`.

### Task 4: Docs And Verification

**Files:**
- Modify: `docs/protocol/tools.md`

- [ ] Document the four new tools with compact request examples and returned data.
- [ ] Run `cargo fmt --check`, `cargo test`, `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Sync plugin source into `MCPTester`, build `MCPTesterEditor`, and run a live MCP smoke test through Unreal.
- [ ] Commit docs with `docs: document landscape grounding tools`.
