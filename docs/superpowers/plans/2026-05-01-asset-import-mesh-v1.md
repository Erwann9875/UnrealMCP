# Asset Import Mesh v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editor MCP tools for local asset import, generated mesh assets, asset validation, and simple static mesh operations.

**Architecture:** Extend the Rust protocol/server with compact bulk-oriented asset commands, then implement editor behavior in the Unreal bridge. Keep MCP schemas as plain object schemas and bridge responses compact enough for repeated LLM-driven scene construction.

**Tech Stack:** Rust, serde, MCP stdio JSON-RPC, UE 5.7 C++ editor plugin, AssetTools, MeshDescription, StaticMeshDescription.

---

### Task 1: Protocol Types

**Files:**
- Modify: `crates/unreal-mcp-protocol/src/commands.rs`
- Test: `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] Add failing round-trip tests for `AssetImportTexture`, `AssetImportStaticMesh`, `AssetBulkImport`, `AssetValidate`, `MeshCreateBuilding`, `MeshCreateSign`, and `StaticMeshSetCollision` plus their result payloads.
- [ ] Add request structs for asset import specs, validation specs, generated building specs, generated sign specs, and static mesh collision specs.
- [ ] Add compact operation/result structs that preserve bulk item order.
- [ ] Add command and result enum variants.
- [ ] Run `cargo test -p unreal-mcp-protocol --test codec_roundtrip`.
- [ ] Commit with `feat(protocol): add asset import mesh commands`.

### Task 2: MCP Server Tools

**Files:**
- Modify: `crates/unreal-mcp-server/src/tools.rs`
- Modify: `crates/unreal-mcp-server/src/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/mcp_stdio.rs`
- Modify: `crates/unreal-mcp-server/tests/connection_tools.rs`
- Modify: `crates/unreal-mcp-tests/src/lib.rs`

- [ ] Add failing MCP tests for tool listing, object-only schema shape, and representative calls for each new tool.
- [ ] Implement argument structs, default values, command dispatch, result extraction, and compact response summaries.
- [ ] Update fake bridge capabilities and handlers for new protocol variants.
- [ ] Run `cargo test -p unreal-mcp-server --test mcp_stdio` and `cargo test -p unreal-mcp-server --test connection_tools connection_tools_return_compact_capabilities_response`.
- [ ] Commit with `feat(server): add asset import mesh tools`.

### Task 3: Unreal Bridge

**Files:**
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`

- [ ] Add editor dependencies required for automated import and static mesh generation.
- [ ] Add helpers for destination path validation, asset package creation, automated import, generated mesh construction, and static mesh lookup.
- [ ] Add bridge capabilities and handlers for the seven commands.
- [ ] Run UE `RunUAT BuildPlugin` with a short package path.
- [ ] Commit with `feat(unreal): add asset import mesh bridge commands`.

### Task 4: Docs And Verification

**Files:**
- Modify: `docs/protocol/tools.md`

- [ ] Document the seven new tools with compact request examples and returned data.
- [ ] Run `cargo fmt --check`, `cargo test`, `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Sync plugin source into `MCPTester`, build `MCPTesterEditor`, and run a live MCP smoke test through Unreal.
- [ ] Commit docs with `docs: document asset import mesh tools`.
