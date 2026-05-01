# MCP Tools

This foundation exposes the initial `connection` tool surface over minimal MCP
stdio JSON-RPC dispatch. It supports `initialize`, `tools/list`, and
`tools/call` for the tools below.

The Rust server connects to the Unreal bridge at `UNREAL_MCP_BRIDGE_ADDR`, or
`127.0.0.1:55557` when the environment variable is not set.

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

Returns compact connection and Unreal version details.

Default response data shape:

```json
{
  "connected": true,
  "bridge_version": "0.1.0",
  "unreal_version": "5.5.0",
  "elapsed_ms": 1
}
```

## `connection.capabilities`

Returns supported command names.

Default response data shape:

```json
{
  "commands": [
    "connection.ping",
    "connection.status",
    "connection.capabilities"
  ],
  "elapsed_ms": 1
}
```

## MCP Methods

The stdio server currently handles:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

Other MCP features, including resources, prompts, roots, sampling, logging, and
cancellation, are out of scope for this slice.
