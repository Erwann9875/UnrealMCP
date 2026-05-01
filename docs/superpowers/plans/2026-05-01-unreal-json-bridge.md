# Unreal JSON Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working Codex -> Rust MCP server -> Unreal Editor plugin connection for connection tools.

**Architecture:** Rust keeps the existing length-prefixed TCP bridge and adds selectable body formats. Unreal implements a JSON-only listener for the same envelope schema, enough to validate the live editor bridge before adding a MessagePack C++ dependency.

**Tech Stack:** Rust, Tokio, serde, Unreal Engine 5.5+ editor C++ plugin, Unreal sockets, Unreal JSON module.

---

## File Structure

- Modify `crates/unreal-mcp-server/src/bridge_client.rs`: add `BridgeFormat`, JSON encode/decode path, and tests for format parsing.
- Modify `crates/unreal-mcp-server/src/main.rs`: read `UNREAL_MCP_BRIDGE_FORMAT`.
- Modify `crates/unreal-mcp-server/src/lib.rs`: export `BridgeFormat`.
- Modify `crates/unreal-mcp-server/tests/connection_tools.rs`: add a fake JSON bridge test that proves framed JSON interop.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`: add JSON-related modules.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h`: add socket/thread lifecycle members.
- Modify `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`: implement listener, JSON request parsing, command dispatch, and response framing.
- Modify `MCPTester/MCPTester.uproject`: enable `UnrealMCPBridge`.
- Create or update `MCPTester/Plugins/UnrealMCPBridge`: local installed plugin copy for editor testing.
- Modify `docs/protocol/tools.md`: document JSON bootstrap format and env var.

### Task 1: Rust Bridge Format Selection

**Files:**
- Modify: `crates/unreal-mcp-server/src/bridge_client.rs`
- Modify: `crates/unreal-mcp-server/src/lib.rs`
- Modify: `crates/unreal-mcp-server/src/main.rs`
- Test: `crates/unreal-mcp-server/src/bridge_client.rs`

- [ ] **Step 1: Write failing tests**

Add tests that assert `BridgeFormat::parse("json")`, `BridgeFormat::parse("msgpack")`, and invalid values.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p unreal-mcp-server bridge_format`

Expected: fail because `BridgeFormat` does not exist.

- [ ] **Step 3: Implement format selection**

Add `BridgeFormat`, `BridgeClient::with_format`, and JSON encode/decode selection inside `send_once`.

- [ ] **Step 4: Run tests**

Run: `cargo test -p unreal-mcp-server bridge_format`

Expected: pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git add crates/unreal-mcp-server/src/bridge_client.rs crates/unreal-mcp-server/src/lib.rs crates/unreal-mcp-server/src/main.rs
git commit -m "feat(server): add bridge format selection"
```

### Task 2: Rust JSON Bridge Interop Test

**Files:**
- Modify: `crates/unreal-mcp-server/tests/connection_tools.rs`

- [ ] **Step 1: Write failing integration test**

Add a JSON fake bridge test that reads a length-prefixed JSON request, decodes it with `decode_json_request`, writes a length-prefixed JSON response with `encode_json_response`, and asserts `BridgeClient::with_format(..., BridgeFormat::Json)` returns a pong.

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test -p unreal-mcp-server bridge_client_sends_json_ping_and_receives_pong`

Expected: fail until Task 1 implementation exists or fail because the client still writes MessagePack.

- [ ] **Step 3: Implement minimal helper**

Keep the helper in the test file. Do not add new production abstractions beyond Task 1.

- [ ] **Step 4: Run test**

Run: `cargo test -p unreal-mcp-server bridge_client_sends_json_ping_and_receives_pong`

Expected: pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git add crates/unreal-mcp-server/tests/connection_tools.rs
git commit -m "test(server): cover json bridge framing"
```

### Task 3: Unreal JSON Bridge Listener

**Files:**
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h`
- Modify: `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`

- [ ] **Step 1: Add compile dependencies**

Add `Json`, `JsonUtilities`, and `Networking` to private dependencies.

- [ ] **Step 2: Implement listener lifecycle**

Use `FTcpListener` bound to `127.0.0.1:55557`, start it in `FBridgeServer::Start`, and stop it cleanly in `Stop`.

- [ ] **Step 3: Implement framed JSON handling**

Read four big-endian bytes, reject frames larger than 16 MiB, read the UTF-8 JSON body, parse the envelope, execute connection commands, serialize the response envelope, and write a four-byte big-endian length followed by the UTF-8 JSON body.

- [ ] **Step 4: Keep editor thread safe**

Only read static metadata for this slice: bridge version, engine version, and command names. Do not touch world state or UObject mutation from the socket thread.

- [ ] **Step 5: Commit**

Run:

```powershell
git add unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp
git commit -m "feat(unreal): add json bridge listener"
```

### Task 4: Install Plugin Into MCPTester

**Files:**
- Modify: `MCPTester/MCPTester.uproject`
- Create/update: `MCPTester/Plugins/UnrealMCPBridge`
- Modify: `docs/protocol/tools.md`

- [ ] **Step 1: Copy plugin source**

Copy `unreal/UnrealMCPBridge` to `MCPTester/Plugins/UnrealMCPBridge` with source, descriptor, and no generated build output.

- [ ] **Step 2: Enable plugin in project**

Add a `UnrealMCPBridge` plugin entry to `MCPTester/MCPTester.uproject` with `Enabled: true` and `TargetAllowList: ["Editor"]`.

- [ ] **Step 3: Document bridge format**

Document `UNREAL_MCP_BRIDGE_FORMAT=json` and `UNREAL_MCP_BRIDGE_ADDR=127.0.0.1:55557`.

- [ ] **Step 4: Commit source/docs only**

Commit source and docs. Do not commit full template asset content or generated Unreal folders.

Run:

```powershell
git add docs/protocol/tools.md
git commit -m "docs: document json bridge bootstrap"
```

### Task 5: Verification

**Files:**
- No source edits unless verification finds a defect.

- [ ] **Step 1: Rust formatting**

Run: `cargo fmt --check`

Expected: pass.

- [ ] **Step 2: Rust tests**

Run: `cargo test`

Expected: pass.

- [ ] **Step 3: Rust lint**

Run: `cargo clippy --workspace --all-targets -- -D warnings`

Expected: pass.

- [ ] **Step 4: Unreal plugin build**

Run from a PowerShell prompt if Unreal 5.7 is installed:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="C:\Users\Erwann\Documents\UnrealMCP\.worktrees\unreal-json-bridge\unreal\UnrealMCPBridge\UnrealMCPBridge.uplugin" -Package="C:\Users\Erwann\Documents\UnrealMCP\.worktrees\unreal-json-bridge\Build\UnrealMCPBridge" -TargetPlatforms=Win64
```

Expected: pass when UE 5.7 build tooling is present.

- [ ] **Step 5: Smoke test**

After opening `MCPTester` in Unreal Editor, run the Rust server with `UNREAL_MCP_BRIDGE_FORMAT=json` and call `connection.ping`.

Expected: the response includes `bridge_version: "0.1.0"`.
