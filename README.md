# UnrealMCP

High-performance MCP tooling for controlling Unreal Editor 5.5+ from Codex.

Version 1 is editor-only. The system is split into:

- A Rust MCP server with minimal stdio JSON-RPC dispatch for connection tools.
- An Unreal Editor C++ plugin that executes editor commands.
- A localhost bridge protocol optimized for bulk operations and compact responses.

The current foundation includes MCP stdio dispatch for `initialize`,
`tools/list`, and `tools/call` for the initial `connection.*` tools.

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
