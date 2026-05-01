# UnrealMCP

High-performance MCP tooling for controlling Unreal Editor 5.5+ from Codex.

Version 1 is editor-only. The system is split into:

- A Rust MCP server that talks to Codex over stdio.
- An Unreal Editor C++ plugin that executes editor commands.
- A localhost bridge protocol optimized for bulk operations and compact responses.

## First Smoke Test

```powershell
cargo test
cargo run -p unreal-mcp-server -- --help
```

## Design Docs

- `docs/superpowers/specs/2026-05-01-rust-unreal-mcp-design.md`
- `docs/superpowers/plans/2026-05-01-rust-unreal-mcp-foundation.md`
