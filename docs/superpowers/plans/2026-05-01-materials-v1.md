# Materials V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editor-only MCP tools for folder creation, material asset creation, procedural texture creation, and bulk material application.

**Architecture:** Extend the shared Rust protocol first, expose compact MCP tool methods and schemas next, then implement Unreal editor bridge commands using asset/package APIs and static mesh component material assignment. Keep responses compact and bulk-first.

**Tech Stack:** Rust, Tokio, serde, MCP stdio JSON-RPC, Unreal Engine 5.7 editor C++ plugin, Unreal Material/Texture/AssetTools APIs.

---

## File Structure

- Modify `crates/unreal-mcp-protocol/src/commands.rs`: add asset/material command and result types.
- Modify `crates/unreal-mcp-protocol/src/lib.rs`: export new types.
- Modify `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`: cover material command/result round trips.
- Modify `crates/unreal-mcp-tests/src/lib.rs`: fake bridge responses for material/asset commands.
- Modify `crates/unreal-mcp-server/src/tools.rs`: add tool methods and argument mapping.
- Modify `crates/unreal-mcp-server/src/mcp_stdio.rs`: add tool listing, schemas, and dispatch.
- Modify `crates/unreal-mcp-server/tests/mcp_stdio.rs`: cover schema listing and representative calls.
- Modify `crates/unreal-mcp-server/tests/connection_tools.rs`: update capability count.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`: add asset/material modules.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`: implement Unreal commands.
- Modify `docs/protocol/tools.md`: document new tool shapes.

## Tasks

### Task 1: Protocol

- [ ] Write failing round-trip tests for `asset_create_folder`, `material_create`, `material_bulk_apply`, and `world_bulk_set_materials`.
- [ ] Add protocol command/result structs and exports.
- [ ] Run `cargo test -p unreal-mcp-protocol codec_roundtrip`.
- [ ] Commit with `feat(protocol): add material commands`.

### Task 2: Rust MCP Surface

- [ ] Write failing MCP stdio tests for tool schemas and representative `material.create` / `material.bulk_apply` calls.
- [ ] Update fake bridge with deterministic compact results.
- [ ] Add tool methods, argument structs, schemas, and dispatch.
- [ ] Run `cargo test -p unreal-mcp-server --test mcp_stdio` and `cargo test -p unreal-mcp-server --test connection_tools`.
- [ ] Commit with `feat(server): add material tools`.

### Task 3: Unreal Bridge

- [ ] Add Unreal dependencies for AssetTools, MaterialEditor, Engine, and UnrealEd APIs.
- [ ] Implement folder creation.
- [ ] Implement material creation from base color, metallic, roughness, specular, and emissive values.
- [ ] Implement material instance creation and scalar/vector parameter updates.
- [ ] Implement procedural texture creation for `solid` and `checker`.
- [ ] Implement bulk material application by actor names/tags.
- [ ] Build standalone plugin with UE 5.7.
- [ ] Commit with `feat(unreal): add material bridge commands`.

### Task 4: Docs, Install, Live Smoke

- [ ] Document new tools in `docs/protocol/tools.md`.
- [ ] Copy plugin source into `MCPTester/Plugins/UnrealMCPBridge`.
- [ ] Rebuild `MCPTester` editor target.
- [ ] Run live stdio smoke calls for folder creation, material creation, actor spawn, material bulk apply, query, and cleanup.
- [ ] Commit with `docs: document material tools`.

### Task 5: Final Verification

- [ ] Run `cargo fmt --check`.
- [ ] Run `cargo test`.
- [ ] Run `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Run UE 5.7 standalone plugin build.
- [ ] Rebuild installed `MCPTester` plugin.
- [ ] Run live stdio smoke against `MCPTester`.
