# Level World Bulk Ops Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add level lifecycle tools and bulk static-mesh actor operations that work through the Rust MCP server and Unreal editor plugin.

**Architecture:** Extend the shared protocol first, then add Rust MCP tool dispatch, then Unreal JSON bridge execution. Writes use bulk bridge commands and return compact count/name/path summaries.

**Tech Stack:** Rust, Tokio, serde, MCP stdio JSON-RPC, Unreal Engine 5.7 editor C++ plugin, Unreal Editor APIs, Unreal JSON.

---

## File Structure

- Modify `crates/unreal-mcp-protocol/src/commands.rs`: add level/world command and result structs.
- Modify `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`: cover new command/result round trips.
- Modify `crates/unreal-mcp-tests/src/lib.rs`: fake bridge support for new commands.
- Modify `crates/unreal-mcp-server/src/tools.rs`: add `EditorTools` domain wrapper for level/world tools.
- Modify `crates/unreal-mcp-server/src/mcp_stdio.rs`: list and dispatch new MCP tools.
- Modify `crates/unreal-mcp-server/src/main.rs` and `src/lib.rs`: wire/export tools.
- Modify `crates/unreal-mcp-server/tests/mcp_stdio.rs`: cover tool listing and representative calls.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`: parse and execute new JSON commands.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`: add editor modules required for level/world commands.
- Modify `docs/protocol/tools.md`: document new tools and request shapes.
- Copy plugin source into `MCPTester/Plugins/UnrealMCPBridge` after Unreal changes compile.

### Task 1: Protocol Commands

- [ ] Write failing protocol round-trip tests for `level_create`, `world_bulk_spawn`, and `world_query`.
- [ ] Run `cargo test -p unreal-mcp-protocol codec_roundtrip` and confirm failures mention missing variants/types.
- [ ] Add command structs and result structs in `commands.rs`.
- [ ] Run `cargo test -p unreal-mcp-protocol codec_roundtrip`.
- [ ] Commit with `feat(protocol): add level and world commands`.

### Task 2: Fake Bridge And Rust Tool Surface

- [ ] Write failing MCP stdio tests for tool listing and representative calls: `level.list`, `world.bulk_spawn`, `world.query`.
- [ ] Run `cargo test -p unreal-mcp-server mcp_stdio` and confirm failures.
- [ ] Update fake bridge to return compact deterministic results.
- [ ] Add Rust tool methods and stdio dispatch/schema entries.
- [ ] Run `cargo test -p unreal-mcp-server mcp_stdio` and `cargo test -p unreal-mcp-server connection_tools`.
- [ ] Commit with `feat(server): add level and world tool dispatch`.

### Task 3: Unreal Command Execution

- [ ] Add Unreal build dependencies for editor level and actor APIs.
- [ ] Implement JSON parsing helpers for vectors, rotators, scales, and string arrays.
- [ ] Implement `level.list`, `level.create`, `level.open`, and `level.save`.
- [ ] Implement `world.bulk_spawn`, `world.bulk_delete`, `world.query`, and `world.snapshot`.
- [ ] Build standalone plugin with UE 5.7.
- [ ] Commit with `feat(unreal): add level and world bridge commands`.

### Task 4: Docs And MCPTester Install

- [ ] Document the new tools in `docs/protocol/tools.md`.
- [ ] Copy the plugin source to `MCPTester/Plugins/UnrealMCPBridge`.
- [ ] Rebuild `MCPTester` editor target.
- [ ] Run live stdio smoke calls against `MCPTester`.
- [ ] Commit docs with `docs: document level and world tools`.

### Task 5: Final Verification

- [ ] Run `cargo fmt --check`.
- [ ] Run `cargo test`.
- [ ] Run `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Run UE 5.7 standalone plugin build.
- [ ] Rebuild installed `MCPTester` plugin.
- [ ] Run live stdio smoke for `level.list`, `world.bulk_spawn`, `world.query`, and `world.bulk_delete`.
