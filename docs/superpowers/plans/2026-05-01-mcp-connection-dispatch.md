# MCP Connection Dispatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Rust server usable as a minimal stdio MCP server for connection tools.

**Architecture:** The Rust server will keep the existing bridge client and connection tool boundary, then add JSON-RPC 2.0 stdio handling for `initialize`, `notifications/initialized`, `tools/list`, and `tools/call`. Tool calls dispatch to `ConnectionTools`, which sends compact MessagePack bridge commands to Unreal or the fake bridge.

**Tech Stack:** Rust 2021, Tokio, Serde JSON, MCP 2025-03-26 JSON-RPC over stdio, existing MessagePack bridge protocol.

---

## Scope

This slice implements:

- `connection.status` and `connection.capabilities` Rust tool handlers.
- Minimal newline-delimited JSON-RPC stdio server.
- MCP `initialize` response with `tools` capability.
- MCP `tools/list` with three connection tools.
- MCP `tools/call` dispatch for `connection.ping`, `connection.status`, and `connection.capabilities`.
- CLI bridge address configuration through `UNREAL_MCP_BRIDGE_ADDR`, defaulting to `127.0.0.1:55557`.
- Tests against the fake bridge.
- Docs updated from “deferred stdio dispatch” to “minimal connection dispatch”.

Out of scope:

- Full MCP batching.
- Resources, prompts, roots, sampling, logging, and cancellation.
- Level/world/material/landscape/Blueprint commands.
- Full Unreal TCP command execution; that follows after the Rust MCP endpoint is usable.

## File Structure

- Modify `crates/unreal-mcp-server/src/tools.rs`: add `status()` and `capabilities()` handlers.
- Modify `crates/unreal-mcp-server/src/mcp_stdio.rs`: add minimal JSON-RPC stdio dispatcher.
- Modify `crates/unreal-mcp-server/src/main.rs`: create `BridgeClient` from env/default address and pass tools into stdio server.
- Modify `crates/unreal-mcp-server/tests/connection_tools.rs`: add handler tests.
- Create `crates/unreal-mcp-server/tests/mcp_stdio.rs`: add MCP lifecycle/list/call tests.
- Modify `README.md` and `docs/protocol/tools.md`: document current usable connection dispatch.

## Task 1: Connection Tool Handlers

**Files:**
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/crates/unreal-mcp-server/src/tools.rs`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/crates/unreal-mcp-server/tests/connection_tools.rs`

- [ ] **Step 1: Write failing tests**

Add tests that call `ConnectionTools::status()` and `ConnectionTools::capabilities()` against `start_fake_bridge()`. Assert compact `ToolResponse` names, summaries, and JSON data fields.

- [ ] **Step 2: Run red tests**

Run: `cargo test -p unreal-mcp-server --test connection_tools`
Expected: fail because `status` and `capabilities` do not exist.

- [ ] **Step 3: Implement handlers**

Add `ConnectionTools::status()` and `ConnectionTools::capabilities()` using `Command::Status` and `Command::Capabilities`. Validate `response.ok`, empty errors, and exactly one result of the expected variant.

- [ ] **Step 4: Run green tests**

Run: `cargo test -p unreal-mcp-server --test connection_tools`
Expected: pass.

- [ ] **Step 5: Commit**

Run:
```powershell
git add crates/unreal-mcp-server/src/tools.rs crates/unreal-mcp-server/tests/connection_tools.rs
git commit -m "feat(server): add connection status and capabilities tools"
```

## Task 2: MCP Stdio Dispatch

**Files:**
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/crates/unreal-mcp-server/src/mcp_stdio.rs`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/crates/unreal-mcp-server/src/main.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/crates/unreal-mcp-server/tests/mcp_stdio.rs`

- [ ] **Step 1: Write failing stdio tests**

Add tests using `tokio::io::duplex` that send newline-delimited JSON-RPC messages and assert responses for:

- `initialize`
- `tools/list`
- `tools/call` with `connection.ping`
- unknown tool error

- [ ] **Step 2: Run red tests**

Run: `cargo test -p unreal-mcp-server --test mcp_stdio`
Expected: fail because current stdio server only drains stdin and writes no JSON-RPC responses.

- [ ] **Step 3: Implement dispatcher**

Implement newline-delimited JSON-RPC request handling:

- `initialize`: return protocol version `2025-03-26`, tools capability, and server info.
- `notifications/initialized`: notification only, no response.
- `tools/list`: return the three connection tools with empty object input schemas.
- `tools/call`: dispatch to `ConnectionTools`.
- unknown methods/tools: return JSON-RPC errors.

- [ ] **Step 4: Update main**

Read `UNREAL_MCP_BRIDGE_ADDR` or default to `127.0.0.1:55557`, create `ConnectionTools`, and pass it into `run_stdio_server`.

- [ ] **Step 5: Run green tests**

Run:
```powershell
cargo test -p unreal-mcp-server --test mcp_stdio
cargo test -p unreal-mcp-server
```
Expected: pass.

- [ ] **Step 6: Commit**

Run:
```powershell
git add crates/unreal-mcp-server/src/mcp_stdio.rs crates/unreal-mcp-server/src/main.rs crates/unreal-mcp-server/tests/mcp_stdio.rs
git commit -m "feat(server): add minimal mcp stdio dispatch"
```

## Task 3: Docs And Verification

**Files:**
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/README.md`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/.worktrees/mcp-connection-dispatch/docs/protocol/tools.md`

- [ ] **Step 1: Update docs**

Document that the server now supports minimal MCP stdio dispatch for `initialize`, `tools/list`, and `tools/call` for the three connection tools.

- [ ] **Step 2: Verify**

Run:
```powershell
cargo fmt --check
cargo test
cargo clippy --workspace --all-targets -- -D warnings
git diff --check
git status --short
```
Expected: all pass and clean except staged docs before commit.

- [ ] **Step 3: Commit**

Run:
```powershell
git add README.md docs/protocol/tools.md docs/superpowers/plans/2026-05-01-mcp-connection-dispatch.md
git commit -m "docs: document mcp connection dispatch"
```

## Self-Review

This plan covers the Rust-side usability gap from the foundation branch: connection tools exist, but stdio JSON-RPC dispatch did not. It intentionally leaves full Unreal TCP command execution and game-creation commands to later slices.
