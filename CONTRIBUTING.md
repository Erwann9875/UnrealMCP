# Contributing

## Scope

This repository builds a Rust MCP server and Unreal Editor C++ bridge for high-performance Unreal Editor automation. Version 1 targets Unreal Engine 5.5+ editor workflows only.

## Commit Norm

Use Conventional Commits:

- `feat`: add user-facing functionality.
- `fix`: fix a bug.
- `docs`: documentation-only changes.
- `test`: add or update tests.
- `refactor`: behavior-preserving code restructuring.
- `perf`: improve performance.
- `chore`: tooling, dependency, or maintenance updates.

Examples:

```text
docs: add mcp architecture design
feat(protocol): add request envelope types
fix(server): handle bridge disconnects
test(protocol): add messagepack roundtrip coverage
perf(world): batch actor transform updates
```

## Commit Rules

- Keep commits focused and buildable.
- Do not mix formatting-only changes with feature work.
- Do not include unrelated generated files.
- Prefer small commits for protocol, server, plugin, docs, and tests.
- Use present tense and imperative mood in the subject.
- Keep the subject under 72 characters when practical.

## Branch Norm

- Use short feature branches.
- Preferred branch prefix: `codex/`.
- Example: `codex/rust-mcp-foundation`.

## Formatting

Before committing Rust changes:

```powershell
cargo fmt
cargo test
```

Before committing Unreal plugin changes, build the editor target or plugin when Unreal Engine is available.

## Testing Expectations

- New Rust behavior needs a failing test first, then implementation.
- Protocol changes need binary round-trip tests and JSON debug round-trip tests.
- MCP server changes need fake-bridge integration coverage.
- Unreal command handlers need smoke coverage where editor automation tests are practical.

## Review Standard

Reviews should prioritize:

- Correctness.
- Performance under bulk operations.
- Token-efficient response shapes.
- Stable error handling.
- Compatibility with Unreal Engine 5.5+.
- Clear docs for any public tool or protocol change.
