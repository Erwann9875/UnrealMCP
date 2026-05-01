# Rust Unreal MCP Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first runnable foundation for a Rust MCP server and Unreal Editor bridge plugin.

**Architecture:** The Rust workspace owns protocol types, MessagePack/JSON-debug serialization, a stdio lifetime skeleton, Rust connection tool handlers, and a localhost bridge client. The Unreal plugin skeleton owns editor startup, localhost bridge lifecycle, and ping/status/capability commands so later plans can add MCP JSON-RPC dispatch wiring and editor command domains behind the same protocol.

**Tech Stack:** Rust 2021, Tokio, Serde, rmp-serde, MCP stdio lifetime management with JSON-RPC dispatch deferred, Unreal Engine 5.5+ C++ editor plugin.

---

## Scope

This plan implements the buildable foundation only:

- Rust workspace with protocol, server, and integration-test crates.
- Request/response envelope types.
- MessagePack and JSON-debug protocol round trips.
- Compact response modes and indexed errors.
- Fake Unreal bridge for Rust integration tests.
- Rust bridge client with reconnect-friendly connection status.
- Stdio lifetime skeleton plus Rust connection tool handlers; MCP JSON-RPC dispatch wiring is deferred to a follow-up plan.
- Unreal Editor plugin skeleton with ping/status/capabilities handlers.
- Documentation for running the first smoke tests.

Out of scope for this plan:

- Actor, level, material, landscape, Blueprint, lighting, and scene helper commands.
- Vendored C++ MessagePack dependency integration.
- Full MCP JSON-RPC stdio dispatcher and stdio tool registration for `connection.status` and `connection.capabilities`.
- Packaged game/runtime support.

Those command domains should be implemented as follow-up plans after this foundation compiles and passes tests.

## File Structure

- Create `Cargo.toml`: Rust workspace manifest.
- Create `.gitignore`: Rust, Unreal, and editor build artifacts.
- Create `.rustfmt.toml`: formatting baseline.
- Modify `README.md`: project overview and smoke-test commands.
- Create `crates/unreal-mcp-protocol/Cargo.toml`: protocol crate manifest.
- Create `crates/unreal-mcp-protocol/src/lib.rs`: public protocol modules.
- Create `crates/unreal-mcp-protocol/src/envelope.rs`: request/response envelopes.
- Create `crates/unreal-mcp-protocol/src/commands.rs`: command/result enums.
- Create `crates/unreal-mcp-protocol/src/error.rs`: protocol error model.
- Create `crates/unreal-mcp-protocol/src/codec.rs`: MessagePack and JSON-debug codec.
- Create `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`: protocol tests.
- Create `crates/unreal-mcp-server/Cargo.toml`: server crate manifest.
- Create `crates/unreal-mcp-server/src/main.rs`: executable entry point.
- Create `crates/unreal-mcp-server/src/lib.rs`: public server modules.
- Create `crates/unreal-mcp-server/src/bridge_client.rs`: TCP bridge client.
- Create `crates/unreal-mcp-server/src/mcp_stdio.rs`: stdio lifetime skeleton.
- Create `crates/unreal-mcp-server/src/tools.rs`: Rust connection tool handlers.
- Create `crates/unreal-mcp-server/tests/connection_tools.rs`: fake bridge integration tests.
- Create `crates/unreal-mcp-tests/Cargo.toml`: test helper crate manifest.
- Create `crates/unreal-mcp-tests/src/lib.rs`: fake bridge utilities.
- Create `unreal/UnrealMCPBridge/UnrealMCPBridge.uplugin`: Unreal plugin descriptor.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`: module dependencies.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/UnrealMCPBridgeModule.h`: module class.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/UnrealMCPBridgeModule.cpp`: startup/shutdown.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h`: Unreal bridge server interface.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`: localhost listener skeleton.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.h`: command declarations.
- Create `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.cpp`: ping/status/capabilities responses.
- Create `docs/protocol/envelope.md`: envelope documentation.
- Create `docs/protocol/tools.md`: first tool documentation.

---

## Task 1: Workspace Foundation

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/Cargo.toml`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/.gitignore`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/.rustfmt.toml`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/README.md`

- [ ] **Step 1: Create workspace files**

Add `Cargo.toml`:

```toml
[workspace]
members = [
    "crates/unreal-mcp-protocol",
    "crates/unreal-mcp-server",
    "crates/unreal-mcp-tests",
]
resolver = "2"

[workspace.package]
edition = "2021"
license = "MIT"
repository = "https://github.com/Erwann9875/UnrealMCP"

[workspace.dependencies]
anyhow = "1"
rmp-serde = "1"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
thiserror = "1"
tokio = { version = "1", features = ["io-util", "macros", "net", "rt-multi-thread", "sync", "time"] }
tracing = "0.1"
tracing-subscriber = { version = "0.3", features = ["env-filter", "fmt"] }
uuid = { version = "1", features = ["serde", "v4"] }
```

Add `.gitignore`:

```gitignore
/target/
/.idea/
/.vscode/
*.user
*.suo
*.sln.DotSettings.user

# Unreal build artifacts
Binaries/
DerivedDataCache/
Intermediate/
Saved/
.vs/
```

Add `.rustfmt.toml`:

```toml
edition = "2021"
newline_style = "Windows"
max_width = 100
```

Replace `README.md` with:

```markdown
# UnrealMCP

High-performance MCP tooling for controlling Unreal Editor 5.5+ from Codex.

Version 1 is editor-only. The system is split into:

- A Rust MCP server foundation with a stdio lifetime skeleton.
- An Unreal Editor C++ plugin that executes editor commands.
- A localhost bridge protocol optimized for bulk operations and compact responses.

The current foundation includes Rust connection tool handlers and bridge
protocol coverage. MCP JSON-RPC stdio dispatch wiring, including stdio tool
registration for `connection.status` and `connection.capabilities`, is deferred
to a follow-up plan.

## First Smoke Test

```powershell
cargo test
"" | cargo run -p unreal-mcp-server
```

## Design Docs

- `docs/superpowers/specs/2026-05-01-rust-unreal-mcp-design.md`
- `docs/superpowers/plans/2026-05-01-rust-unreal-mcp-foundation.md`
```

- [ ] **Step 2: Verify workspace manifest fails before crates exist**

Run:

```powershell
cargo metadata --no-deps
```

Expected: failure mentioning missing workspace member `crates/unreal-mcp-protocol`.

- [ ] **Step 3: Commit**

```powershell
git add Cargo.toml .gitignore .rustfmt.toml README.md
git commit -m "chore: add rust workspace baseline"
```

---

## Task 2: Protocol Crate Types

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/Cargo.toml`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/lib.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/envelope.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/commands.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/error.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] **Step 1: Write failing protocol type test**

Add `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`:

```rust
use unreal_mcp_protocol::{
    Command, CommandResult, ErrorMode, RequestEnvelope, ResponseEnvelope, ResponseMode,
};

#[test]
fn request_envelope_keeps_bulk_commands_ordered() {
    let envelope = RequestEnvelope::new(
        7,
        ResponseMode::Handles,
        ErrorMode::Continue,
        vec![Command::Ping, Command::Capabilities],
    );

    assert_eq!(envelope.protocol_version, 1);
    assert_eq!(envelope.request_id, 7);
    assert_eq!(envelope.commands.len(), 2);
    assert!(matches!(envelope.commands[0], Command::Ping));
    assert!(matches!(envelope.commands[1], Command::Capabilities));
}

#[test]
fn response_envelope_can_return_compact_success() {
    let response = ResponseEnvelope::success(
        7,
        3,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );

    assert!(response.ok);
    assert_eq!(response.request_id, 7);
    assert_eq!(response.elapsed_ms, 3);
    assert_eq!(response.errors.len(), 0);
    assert_eq!(response.results.len(), 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cargo test -p unreal-mcp-protocol --test codec_roundtrip
```

Expected: failure because package `unreal-mcp-protocol` does not exist yet.

- [ ] **Step 3: Add protocol crate implementation**

Add `crates/unreal-mcp-protocol/Cargo.toml`:

```toml
[package]
name = "unreal-mcp-protocol"
version = "0.1.0"
edition.workspace = true
license.workspace = true
repository.workspace = true

[dependencies]
serde.workspace = true
thiserror.workspace = true
uuid.workspace = true
```

Add `crates/unreal-mcp-protocol/src/lib.rs`:

```rust
pub mod commands;
pub mod envelope;
pub mod error;

pub use commands::{BridgeStatus, Command, CommandResult};
pub use envelope::{ErrorMode, IndexedError, RequestEnvelope, ResponseEnvelope, ResponseMode};
pub use error::{ProtocolError, ProtocolResult};
```

Add `crates/unreal-mcp-protocol/src/commands.rs`:

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", tag = "type", content = "data")]
pub enum Command {
    Ping,
    Status,
    Capabilities,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BridgeStatus {
    pub connected: bool,
    pub bridge_version: Option<String>,
    pub unreal_version: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case", tag = "type", content = "data")]
pub enum CommandResult {
    Pong { bridge_version: String },
    Status(BridgeStatus),
    Capabilities { commands: Vec<String> },
}
```

Add `crates/unreal-mcp-protocol/src/envelope.rs`:

```rust
use serde::{Deserialize, Serialize};

use crate::Command;
use crate::CommandResult;

pub const PROTOCOL_VERSION: u16 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ResponseMode {
    Summary,
    Handles,
    Full,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorMode {
    Stop,
    Continue,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RequestEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    pub response_mode: ResponseMode,
    pub error_mode: ErrorMode,
    pub commands: Vec<Command>,
}

impl RequestEnvelope {
    pub fn new(
        request_id: u64,
        response_mode: ResponseMode,
        error_mode: ErrorMode,
        commands: Vec<Command>,
    ) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            response_mode,
            error_mode,
            commands,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct IndexedError {
    pub command_index: usize,
    pub item_index: Option<usize>,
    pub code: String,
    pub message: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResponseEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    pub ok: bool,
    pub elapsed_ms: u32,
    pub results: Vec<CommandResult>,
    pub errors: Vec<IndexedError>,
}

impl ResponseEnvelope {
    pub fn success(request_id: u64, elapsed_ms: u32, results: Vec<CommandResult>) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            ok: true,
            elapsed_ms,
            results,
            errors: Vec::new(),
        }
    }

    pub fn failure(request_id: u64, elapsed_ms: u32, errors: Vec<IndexedError>) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            ok: false,
            elapsed_ms,
            results: Vec::new(),
            errors,
        }
    }
}
```

Add `crates/unreal-mcp-protocol/src/error.rs`:

```rust
use thiserror::Error;

pub type ProtocolResult<T> = Result<T, ProtocolError>;

#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("unsupported protocol version {actual}, expected {expected}")]
    UnsupportedProtocolVersion { actual: u16, expected: u16 },

    #[error("message serialization failed: {0}")]
    Encode(String),

    #[error("message deserialization failed: {0}")]
    Decode(String),
}
```

- [ ] **Step 4: Run protocol type tests**

Run:

```powershell
cargo test -p unreal-mcp-protocol --test codec_roundtrip
```

Expected: pass.

- [ ] **Step 5: Commit**

```powershell
git add crates/unreal-mcp-protocol
git commit -m "feat(protocol): add bridge envelope types"
```

---

## Task 3: Protocol Codec

**Files:**
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/Cargo.toml`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/lib.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/src/codec.rs`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`

- [ ] **Step 1: Write failing codec tests**

Append to `crates/unreal-mcp-protocol/tests/codec_roundtrip.rs`:

```rust
use unreal_mcp_protocol::{decode_msgpack_request, encode_msgpack_request};
use unreal_mcp_protocol::{decode_json_request, encode_json_request};

#[test]
fn msgpack_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        42,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::Ping],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn json_debug_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        43,
        ResponseMode::Full,
        ErrorMode::Continue,
        vec![Command::Status],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```powershell
cargo test -p unreal-mcp-protocol --test codec_roundtrip
```

Expected: failure because codec functions are not defined.

- [ ] **Step 3: Add codec implementation**

Add dependencies to `crates/unreal-mcp-protocol/Cargo.toml`:

```toml
[dependencies]
rmp-serde.workspace = true
serde.workspace = true
serde_json.workspace = true
thiserror.workspace = true
uuid.workspace = true
```

Add `pub mod codec;` and exports to `crates/unreal-mcp-protocol/src/lib.rs`:

```rust
pub mod codec;
pub mod commands;
pub mod envelope;
pub mod error;

pub use codec::{
    decode_json_request, decode_json_response, decode_msgpack_request, decode_msgpack_response,
    encode_json_request, encode_json_response, encode_msgpack_request, encode_msgpack_response,
};
pub use commands::{BridgeStatus, Command, CommandResult};
pub use envelope::{ErrorMode, IndexedError, RequestEnvelope, ResponseEnvelope, ResponseMode};
pub use error::{ProtocolError, ProtocolResult};
```

Add `crates/unreal-mcp-protocol/src/codec.rs`:

```rust
use crate::{ProtocolError, ProtocolResult, RequestEnvelope, ResponseEnvelope};

pub fn encode_msgpack_request(value: &RequestEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_request(bytes: &[u8]) -> ProtocolResult<RequestEnvelope> {
    rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_msgpack_response(value: &ResponseEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_response(bytes: &[u8]) -> ProtocolResult<ResponseEnvelope> {
    rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_json_request(value: &RequestEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_request(text: &str) -> ProtocolResult<RequestEnvelope> {
    serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_json_response(value: &ResponseEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_response(text: &str) -> ProtocolResult<ResponseEnvelope> {
    serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))
}
```

- [ ] **Step 4: Run codec tests**

Run:

```powershell
cargo test -p unreal-mcp-protocol --test codec_roundtrip
```

Expected: pass.

- [ ] **Step 5: Commit**

```powershell
git add crates/unreal-mcp-protocol
git commit -m "feat(protocol): add messagepack and json codecs"
```

---

## Task 4: Fake Bridge Test Utilities

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-tests/Cargo.toml`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-tests/src/lib.rs`

- [ ] **Step 1: Write fake bridge crate**

Add `crates/unreal-mcp-tests/Cargo.toml`:

```toml
[package]
name = "unreal-mcp-tests"
version = "0.1.0"
edition.workspace = true
license.workspace = true
repository.workspace = true

[dependencies]
anyhow.workspace = true
tokio.workspace = true
unreal-mcp-protocol = { path = "../unreal-mcp-protocol" }
```

Add `crates/unreal-mcp-tests/src/lib.rs`:

```rust
use anyhow::Context;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

use unreal_mcp_protocol::{
    decode_msgpack_request, encode_msgpack_response, Command, CommandResult, ResponseEnvelope,
};

pub struct FakeBridge {
    address: String,
}

impl FakeBridge {
    pub fn address(&self) -> &str {
        &self.address
    }
}

pub async fn start_fake_bridge() -> anyhow::Result<FakeBridge> {
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .context("bind fake bridge")?;
    let address = listener.local_addr()?.to_string();

    tokio::spawn(async move {
        loop {
            let Ok((stream, _)) = listener.accept().await else {
                break;
            };
            tokio::spawn(handle_connection(stream));
        }
    });

    Ok(FakeBridge { address })
}

async fn handle_connection(mut stream: TcpStream) -> anyhow::Result<()> {
    let mut length_buf = [0_u8; 4];
    stream.read_exact(&mut length_buf).await?;
    let length = u32::from_be_bytes(length_buf) as usize;
    let mut body = vec![0_u8; length];
    stream.read_exact(&mut body).await?;

    let request = decode_msgpack_request(&body)?;
    let results = request
        .commands
        .into_iter()
        .map(|command| match command {
            Command::Ping => CommandResult::Pong {
                bridge_version: "fake-0.1.0".to_string(),
            },
            Command::Status => CommandResult::Status(unreal_mcp_protocol::BridgeStatus {
                connected: true,
                bridge_version: Some("fake-0.1.0".to_string()),
                unreal_version: Some("fake-unreal".to_string()),
            }),
            Command::Capabilities => CommandResult::Capabilities {
                commands: vec![
                    "connection.ping".to_string(),
                    "connection.status".to_string(),
                    "connection.capabilities".to_string(),
                ],
            },
        })
        .collect();

    let response = ResponseEnvelope::success(request.request_id, 1, results);
    let response_body = encode_msgpack_response(&response)?;
    stream
        .write_all(&(response_body.len() as u32).to_be_bytes())
        .await?;
    stream.write_all(&response_body).await?;
    Ok(())
}
```

- [ ] **Step 2: Run workspace tests**

Run:

```powershell
cargo test
```

Expected: pass for protocol tests and compile the fake bridge crate.

- [ ] **Step 3: Commit**

```powershell
git add crates/unreal-mcp-tests
git commit -m "test: add fake unreal bridge"
```

---

## Task 5: Rust Bridge Client

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/Cargo.toml`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/lib.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/bridge_client.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/tests/connection_tools.rs`

- [ ] **Step 1: Write failing bridge client integration tests**

Add `crates/unreal-mcp-server/tests/connection_tools.rs`:

```rust
use unreal_mcp_protocol::{Command, CommandResult, ErrorMode, RequestEnvelope, ResponseMode};
use unreal_mcp_server::BridgeClient;
use unreal_mcp_tests::start_fake_bridge;

#[tokio::test]
async fn bridge_client_sends_ping_and_receives_pong() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let client = BridgeClient::new(fake.address().to_string());

    let response = client
        .send(RequestEnvelope::new(
            1,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect("bridge response");

    assert!(response.ok);
    assert_eq!(response.results.len(), 1);
    assert!(matches!(
        &response.results[0],
        CommandResult::Pong { bridge_version } if bridge_version == "fake-0.1.0"
    ));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cargo test -p unreal-mcp-server --test connection_tools
```

Expected: failure because `unreal-mcp-server` does not exist.

- [ ] **Step 3: Add bridge client implementation**

Add `crates/unreal-mcp-server/Cargo.toml`:

```toml
[package]
name = "unreal-mcp-server"
version = "0.1.0"
edition.workspace = true
license.workspace = true
repository.workspace = true

[dependencies]
anyhow.workspace = true
tokio.workspace = true
tracing.workspace = true
tracing-subscriber.workspace = true
unreal-mcp-protocol = { path = "../unreal-mcp-protocol" }

[dev-dependencies]
unreal-mcp-tests = { path = "../unreal-mcp-tests" }
```

Add `crates/unreal-mcp-server/src/lib.rs`:

```rust
pub mod bridge_client;

pub use bridge_client::BridgeClient;
```

Add `crates/unreal-mcp-server/src/bridge_client.rs`:

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

use unreal_mcp_protocol::{
    decode_msgpack_response, encode_msgpack_request, RequestEnvelope, ResponseEnvelope,
};

#[derive(Debug, Clone)]
pub struct BridgeClient {
    address: String,
}

impl BridgeClient {
    pub fn new(address: String) -> Self {
        Self { address }
    }

    pub fn address(&self) -> &str {
        &self.address
    }

    pub async fn send(&self, request: RequestEnvelope) -> anyhow::Result<ResponseEnvelope> {
        let mut stream = TcpStream::connect(&self.address).await?;
        let body = encode_msgpack_request(&request)?;
        stream.write_all(&(body.len() as u32).to_be_bytes()).await?;
        stream.write_all(&body).await?;

        let mut length_buf = [0_u8; 4];
        stream.read_exact(&mut length_buf).await?;
        let length = u32::from_be_bytes(length_buf) as usize;
        let mut response_body = vec![0_u8; length];
        stream.read_exact(&mut response_body).await?;

        Ok(decode_msgpack_response(&response_body)?)
    }
}
```

- [ ] **Step 4: Run bridge client tests**

Run:

```powershell
cargo test -p unreal-mcp-server --test connection_tools
```

Expected: pass.

- [ ] **Step 5: Commit**

```powershell
git add crates/unreal-mcp-server
git commit -m "feat(server): add bridge client"
```

---

## Task 6: MCP Stdio Lifetime Skeleton

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/main.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/mcp_stdio.rs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/tools.rs`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/src/lib.rs`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/Cargo.toml`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/crates/unreal-mcp-server/tests/connection_tools.rs`

- [ ] **Step 1: Write failing tool handler test**

Append to `crates/unreal-mcp-server/tests/connection_tools.rs`:

```rust
use unreal_mcp_server::{ConnectionTools, ToolResponse};

#[tokio::test]
async fn connection_tools_return_compact_ping_response() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let response = tools.ping().await.expect("ping response");

    assert_eq!(response.tool_name, "connection.ping");
    assert!(response.summary.contains("fake-0.1.0"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cargo test -p unreal-mcp-server --test connection_tools
```

Expected: failure because `ConnectionTools` and `ToolResponse` do not exist.

- [ ] **Step 3: Add tool handler implementation**

Add dependency to `crates/unreal-mcp-server/Cargo.toml`:

```toml
serde_json.workspace = true
```

Update `crates/unreal-mcp-server/src/lib.rs`:

```rust
pub mod bridge_client;
pub mod mcp_stdio;
pub mod tools;

pub use bridge_client::BridgeClient;
pub use tools::{ConnectionTools, ToolResponse};
```

Add `crates/unreal-mcp-server/src/tools.rs`:

```rust
use serde_json::json;

use unreal_mcp_protocol::{
    Command, CommandResult, ErrorMode, RequestEnvelope, ResponseMode,
};

use crate::BridgeClient;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolResponse {
    pub tool_name: &'static str,
    pub summary: String,
    pub data: serde_json::Value,
}

#[derive(Debug, Clone)]
pub struct ConnectionTools {
    bridge: BridgeClient,
}

impl ConnectionTools {
    pub fn new(bridge: BridgeClient) -> Self {
        Self { bridge }
    }

    pub async fn ping(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                1,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Ping],
            ))
            .await?;

        let bridge_version = match response.results.first() {
            Some(CommandResult::Pong { bridge_version }) => bridge_version.clone(),
            _ => "unknown".to_string(),
        };

        Ok(ToolResponse {
            tool_name: "connection.ping",
            summary: format!("Unreal bridge responded with version {bridge_version}."),
            data: json!({
                "ok": response.ok,
                "bridge_version": bridge_version,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }
}
```

Add `crates/unreal-mcp-server/src/mcp_stdio.rs`. This is only the stdio
lifetime skeleton; MCP JSON-RPC request parsing and tool dispatch are deferred
to a follow-up plan.

```rust
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite};

pub async fn run_stdio_server<R, W>(mut reader: R, _writer: W) -> anyhow::Result<()>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let mut buffer = [0_u8; 8192];
    loop {
        if reader.read(&mut buffer).await? == 0 {
            return Ok(());
        }
    }
}
```

Add `crates/unreal-mcp-server/src/main.rs`:

```rust
#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            std::env::var("RUST_LOG").unwrap_or_else(|_| "unreal_mcp_server=info".to_string()),
        )
        .with_writer(std::io::stderr)
        .init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();
    unreal_mcp_server::mcp_stdio::run_stdio_server(stdin, stdout).await
}
```

- [ ] **Step 4: Run server tests**

Run:

```powershell
cargo test -p unreal-mcp-server --test connection_tools
```

Expected: pass.

- [ ] **Step 5: Verify executable starts**

Run:

```powershell
"" | cargo run -p unreal-mcp-server
```

Expected: process starts, reads stdin until EOF, and exits cleanly.

- [ ] **Step 6: Commit**

```powershell
git add crates/unreal-mcp-server
git commit -m "feat(server): add connection tool skeleton"
```

---

## Task 7: Unreal Editor Plugin Skeleton

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/UnrealMCPBridge.uplugin`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/UnrealMCPBridgeModule.h`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/UnrealMCPBridgeModule.cpp`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.h`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.cpp`

- [ ] **Step 1: Add plugin descriptor**

Add `unreal/UnrealMCPBridge/UnrealMCPBridge.uplugin`:

```json
{
  "FileVersion": 3,
  "Version": 1,
  "VersionName": "0.1.0",
  "FriendlyName": "Unreal MCP Bridge",
  "Description": "Editor bridge for high-performance MCP automation.",
  "Category": "Editor",
  "CreatedBy": "UnrealMCP",
  "CanContainContent": false,
  "IsBetaVersion": true,
  "Installed": false,
  "Modules": [
    {
      "Name": "UnrealMCPBridge",
      "Type": "Editor",
      "LoadingPhase": "PostEngineInit"
    }
  ]
}
```

- [ ] **Step 2: Add Unreal module files**

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/UnrealMCPBridge.Build.cs`:

```csharp
using UnrealBuildTool;

public class UnrealMCPBridge : ModuleRules
{
    public UnrealMCPBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "EditorSubsystem",
            "Projects",
            "Sockets",
            "UnrealEd"
        });
    }
}
```

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Public/UnrealMCPBridgeModule.h`:

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FBridgeServer;

class FUnrealMCPBridgeModule final : public IModuleInterface
{
public:
    void StartupModule() override;
    void ShutdownModule() override;

private:
    TUniquePtr<FBridgeServer> BridgeServer;
};
```

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/UnrealMCPBridgeModule.cpp`:

```cpp
#include "UnrealMCPBridgeModule.h"

#include "Bridge/BridgeServer.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridge, Log, All);

void FUnrealMCPBridgeModule::StartupModule()
{
    BridgeServer = MakeUnique<FBridgeServer>();
    BridgeServer->Start();
    UE_LOG(LogUnrealMCPBridge, Display, TEXT("Unreal MCP Bridge module started."));
}

void FUnrealMCPBridgeModule::ShutdownModule()
{
    if (BridgeServer)
    {
        BridgeServer->Stop();
        BridgeServer.Reset();
    }
    UE_LOG(LogUnrealMCPBridge, Display, TEXT("Unreal MCP Bridge module stopped."));
}

IMPLEMENT_MODULE(FUnrealMCPBridgeModule, UnrealMCPBridge)
```

- [ ] **Step 3: Add bridge server skeleton**

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

class FBridgeServer
{
public:
    FBridgeServer();
    ~FBridgeServer();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    bool bRunning;
};
```

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Bridge/BridgeServer.cpp`:

```cpp
#include "Bridge/BridgeServer.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPBridgeServer, Log, All);

FBridgeServer::FBridgeServer()
    : bRunning(false)
{
}

FBridgeServer::~FBridgeServer()
{
    Stop();
}

bool FBridgeServer::Start()
{
    bRunning = true;
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Bridge server marked running on localhost."));
    return bRunning;
}

void FBridgeServer::Stop()
{
    if (!bRunning)
    {
        return;
    }

    bRunning = false;
    UE_LOG(LogUnrealMCPBridgeServer, Display, TEXT("Bridge server stopped."));
}

bool FBridgeServer::IsRunning() const
{
    return bRunning;
}
```

- [ ] **Step 4: Add connection command skeleton**

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

class FConnectionCommands
{
public:
    static FString Ping();
    static FString Status(bool bBridgeRunning);
    static FString Capabilities();
};
```

Add `unreal/UnrealMCPBridge/Source/UnrealMCPBridge/Private/Commands/ConnectionCommands.cpp`:

```cpp
#include "Commands/ConnectionCommands.h"

FString FConnectionCommands::Ping()
{
    return TEXT("{\"type\":\"pong\",\"data\":{\"bridge_version\":\"0.1.0\"}}");
}

FString FConnectionCommands::Status(bool bBridgeRunning)
{
    return FString::Printf(
        TEXT("{\"type\":\"status\",\"data\":{\"connected\":%s,\"bridge_version\":\"0.1.0\",\"unreal_version\":\"%s\"}}"),
        bBridgeRunning ? TEXT("true") : TEXT("false"),
        *FEngineVersion::Current().ToString());
}

FString FConnectionCommands::Capabilities()
{
    return TEXT("{\"type\":\"capabilities\",\"data\":{\"commands\":[\"connection.ping\",\"connection.status\",\"connection.capabilities\"]}}");
}
```

- [ ] **Step 5: Verify plugin file layout**

Run:

```powershell
Get-ChildItem -Recurse unreal/UnrealMCPBridge | Select-Object FullName
```

Expected: descriptor, Build.cs, module files, bridge files, and command files are present.

- [ ] **Step 6: Commit**

```powershell
git add unreal/UnrealMCPBridge
git commit -m "feat(unreal): add editor bridge plugin skeleton"
```

---

## Task 8: Protocol And Tool Docs

**Files:**
- Create: `C:/Users/Erwann/Documents/UnrealMCP/docs/protocol/envelope.md`
- Create: `C:/Users/Erwann/Documents/UnrealMCP/docs/protocol/tools.md`
- Modify: `C:/Users/Erwann/Documents/UnrealMCP/README.md`

- [ ] **Step 1: Add envelope docs**

Add `docs/protocol/envelope.md`:

```markdown
# Bridge Envelope

The Rust MCP server communicates with the Unreal Editor bridge over localhost TCP.

Every binary request is length-prefixed:

```text
u32 big-endian payload_length
MessagePack RequestEnvelope
```

Every binary response uses the same frame shape:

```text
u32 big-endian payload_length
MessagePack ResponseEnvelope
```

Version 1 uses protocol version `1`.

## Request Fields

- `protocol_version`: protocol version.
- `request_id`: caller-provided request id.
- `response_mode`: `summary`, `handles`, or `full`.
- `error_mode`: `stop` or `continue`.
- `commands`: ordered command list.

## Response Fields

- `protocol_version`: protocol version.
- `request_id`: mirrors the request id.
- `ok`: true when the batch succeeded.
- `elapsed_ms`: bridge execution time in milliseconds.
- `results`: ordered command results.
- `errors`: indexed command or item failures.
```

- [ ] **Step 2: Add tool docs**

Add `docs/protocol/tools.md`:

```markdown
# MCP Tools

The first implemented protocol group is `connection`. In this foundation,
only `connection.ping` has a Rust tool handler and the MCP stdio path remains
a lifetime skeleton. MCP JSON-RPC dispatch wiring and stdio registration for
`connection.status` and `connection.capabilities` are deferred.

## `connection.ping`

Checks whether the Unreal bridge responds.

Default response shape:

```json
{
  "ok": true,
  "bridge_version": "0.1.0",
  "elapsed_ms": 1
}
```

## `connection.status`

Protocol command and Unreal bridge skeleton entry. It is not wired as an MCP
stdio tool in this foundation. Planned MCP dispatch wiring will return compact
connection and Unreal version details.

Bridge result shape:

```json
{
  "type": "status",
  "data": {
    "connected": true,
    "bridge_version": "0.1.0",
    "unreal_version": "5.5.0"
  }
}
```

## `connection.capabilities`

Protocol command and Unreal bridge skeleton entry. It is not wired as an MCP
stdio tool in this foundation. Planned MCP dispatch wiring will return
supported command names. The current bridge/protocol shape is:

```json
{
  "type": "capabilities",
  "data": {
    "commands": [
      "connection.ping",
      "connection.status",
      "connection.capabilities"
    ]
  }
}
```
```

- [ ] **Step 3: Update README docs list**

Add this section to `README.md`:

```markdown
## Protocol Docs

- `docs/protocol/envelope.md`
- `docs/protocol/tools.md`
```

- [ ] **Step 4: Commit**

```powershell
git add README.md docs/protocol/envelope.md docs/protocol/tools.md
git commit -m "docs: document foundation protocol"
```

---

## Task 9: Final Verification

**Files:**
- No file edits.

- [ ] **Step 1: Run Rust formatting**

Run:

```powershell
cargo fmt
```

Expected: command exits successfully.

- [ ] **Step 2: Run Rust tests**

Run:

```powershell
cargo test
```

Expected: all Rust tests pass.

- [ ] **Step 3: Check Git status**

Run:

```powershell
git status --short
```

Expected: no uncommitted changes.

- [ ] **Step 4: Record Unreal compile status**

If Unreal Engine 5.5+ is installed, copy or symlink `unreal/UnrealMCPBridge` into a UE project `Plugins` folder and build the editor target.

Expected when UE is available: plugin compiles.

Expected when UE is not available in the current shell: document that Rust tests passed and Unreal compile was not run.

## Self-Review

Coverage check:

- Workspace baseline is covered by Task 1.
- Protocol types are covered by Task 2.
- Binary and JSON-debug codecs are covered by Task 3.
- Fake bridge is covered by Task 4.
- Bridge client is covered by Task 5.
- MCP server skeleton is covered by Task 6.
- Unreal plugin skeleton is covered by Task 7.
- Protocol docs are covered by Task 8.
- Final verification is covered by Task 9.

No actor, material, landscape, Blueprint, lighting, or scene helper behavior is included in this foundation plan. Those are intentionally split into follow-up plans after this foundation is green.
