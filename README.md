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

## Protocol Docs

- [Envelope](docs/protocol/envelope.md)
- [Tools](docs/protocol/tools.md)
