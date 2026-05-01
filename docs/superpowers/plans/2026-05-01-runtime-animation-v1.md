# Runtime Animation V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editor-only MCP tools for actor Blueprint creation, component authoring, reusable runtime animation presets, and preset attachment to actors or Blueprints.

**Architecture:** Extend the shared Rust protocol first, expose compact MCP schemas next, then implement Unreal bridge commands with Blueprint/SCS APIs and a bridge-owned runtime animator component. Avoid raw Blueprint graph editing; use reusable data assets and one component that ticks in Play-in-Editor.

**Tech Stack:** Rust, Tokio, serde, MCP stdio JSON-RPC, Unreal Engine 5.7 editor C++ plugin, Blueprint editor APIs, `UActorComponent`, `UDataAsset`, static mesh/light component APIs.

---

## File Structure

- Modify `crates/unreal-mcp-protocol/src/commands.rs`: add Blueprint/runtime command and result types.
- Modify `crates/unreal-mcp-protocol/src/lib.rs`: export new structs.
- Modify `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`: cover Blueprint/runtime command/result round trips.
- Modify `crates/unreal-mcp-tests/src/lib.rs`: fake bridge responses for Blueprint/runtime commands.
- Modify `crates/unreal-mcp-server/src/tools.rs`: add tool methods, defaulting, and argument mapping.
- Modify `crates/unreal-mcp-server/src/mcp_stdio.rs`: add tool listing, schemas, and dispatch.
- Modify `crates/unreal-mcp-server/tests/mcp_stdio.rs`: cover schemas and representative calls.
- Modify `crates/unreal-mcp-server/tests/connection_tools.rs`: update capability count.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`: add Blueprint graph/editor modules as needed.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/Runtime/UnrealMCPRuntimeAnimationPreset.h`: animation preset data asset.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/Runtime/UnrealMCPRuntimeAnimatorComponent.h`: runtime tick component API.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Runtime/UnrealMCPRuntimeAnimatorComponent.cpp`: runtime animation behavior.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`: implement bridge commands.
- Modify `docs/protocol/tools.md`: document new tools.

## Tasks

### Task 1: Protocol

- [ ] Write failing round-trip tests for Blueprint commands, runtime animation commands, and compact results.
- [ ] Add protocol command/result structs and exports.
- [ ] Run `cargo test -p unreal-mcp-protocol --test codec_roundtrip`.
- [ ] Commit with `feat(protocol): add runtime animation commands`.

### Task 2: Rust MCP Surface

- [ ] Write failing MCP stdio tests for tool listing and representative Blueprint/runtime calls.
- [ ] Update fake bridge with deterministic compact responses.
- [ ] Add tool methods, argument structs, schemas, defaults, and dispatch.
- [ ] Run `cargo test -p unreal-mcp-server --test mcp_stdio` and `cargo test -p unreal-mcp-server --test connection_tools`.
- [ ] Commit with `feat(server): add runtime animation tools`.

### Task 3: Unreal Runtime Types

- [ ] Add `UUnrealMCPRuntimeAnimationPreset` with fields for kind, target component, parameter name, colors, scalar range, speed, amplitude, axis, base intensity, and phase offset.
- [ ] Add `UUnrealMCPRuntimeAnimatorComponent` with an editable preset array.
- [ ] Implement `BeginPlay` dynamic material setup and initial component transform/intensity capture.
- [ ] Implement `TickComponent` for LED/material parameter pulse and moving light animation.
- [ ] Build standalone plugin with UE 5.7.
- [ ] Commit with `feat(unreal): add runtime animator component`.

### Task 4: Unreal Bridge Commands

- [ ] Implement Blueprint asset creation with `FKismetEditorUtilities::CreateBlueprint`.
- [ ] Implement static mesh and light component template addition with Simple Construction Script nodes.
- [ ] Implement Blueprint compile.
- [ ] Implement runtime preset data asset creation.
- [ ] Implement preset attachment to placed actors and Blueprint assets.
- [ ] Build standalone plugin with UE 5.7.
- [ ] Commit with `feat(unreal): add blueprint runtime bridge commands`.

### Task 5: Docs, Install, Live Smoke

- [ ] Document new tools in `docs/protocol/tools.md`.
- [ ] Copy plugin source into `MCPTester/Plugins/UnrealMCPBridge`.
- [ ] Rebuild `MCPTester` editor target.
- [ ] Run live stdio smoke for Blueprint creation, component addition, compile, animation preset creation, actor spawn, preset attach, query, and cleanup.
- [ ] Commit with `docs: document runtime animation tools`.

### Task 6: Final Verification

- [ ] Run `cargo fmt --check`.
- [ ] Run `cargo test`.
- [ ] Run `cargo clippy --workspace --all-targets -- -D warnings`.
- [ ] Run UE 5.7 standalone plugin build.
- [ ] Rebuild installed `MCPTester` plugin.
- [ ] Run live stdio smoke against `MCPTester`.
