# Scene Assembly v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add compact editor-only MCP tools for bulk city scene assembly.

**Architecture:** Add protocol spec/result types, expose MCP stdio schemas and handlers, implement fake bridge support, then implement the Unreal bridge commands in one editor transaction per command. Return compact counts and spawned actor refs to keep token use low.

**Tech Stack:** Rust MCP server/protocol, serde JSON/msgpack, Unreal Engine 5.5+ editor C++ bridge.

---

### Task 1: Protocol Tests And Types

**Files:**
- Modify: `crates/unreal-mcp-protocol/src/commands.rs`
- Modify: `crates/unreal-mcp-protocol/src/lib.rs`
- Modify: `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] Add failing roundtrip tests for `road.create_network`, `scene.bulk_place_on_grid`, `scene.create_city_block`, `scene.create_district`, and `SceneAssemblyResult`.
- [ ] Add command variants and spec/result structs.
- [ ] Export new structs from `lib.rs`.
- [ ] Run `cargo test -p unreal-mcp-protocol`.

### Task 2: MCP Server Tests And Handlers

**Files:**
- Modify: `crates/unreal-mcp-server/src/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/src/tools.rs`
- Modify: `crates/unreal-mcp-server/tests/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/connection_tools.rs`
- Modify: `crates/unreal-mcp-tests/src/lib.rs`

- [ ] Add failing stdio tests for tool list entries, schema shape, and fake bridge dispatch.
- [ ] Add schemas and dispatch for the four tools.
- [ ] Add `ConnectionTools` handlers and JSON response formatting.
- [ ] Update fake bridge capabilities and result handling.
- [ ] Run `cargo test -p unreal-mcp-server -p unreal-mcp-tests`.

### Task 3: Unreal Bridge Commands

**Files:**
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`

- [ ] Add bridge capabilities for the four commands.
- [ ] Add scene actor spawn helpers that apply scene/group/type tags.
- [ ] Implement road network, grid placement, city block, and district generation.
- [ ] Return `scene_assembly` tagged results with compact counts.
- [ ] Run Unreal `BuildPlugin`.

### Task 4: Docs And Verification

**Files:**
- Modify: `docs/protocol/tools.md`

- [ ] Document examples for all four tools.
- [ ] Run `cargo fmt --check`, `cargo test`, and `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Sync the plugin into `MCPTester`, build `MCPTesterEditor`, run live smoke, merge to `main`, rebuild `target-codex`, and push.
