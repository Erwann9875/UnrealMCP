# MCP Tools

This foundation defines the initial `connection` protocol/tool surface.
Only `connection.ping` has a Rust tool handler in this foundation. The
stdio MCP dispatch path is still a skeleton.

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

Protocol command and Unreal bridge skeleton entry. Planned MCP dispatch
wiring will return compact connection and Unreal version details.

## `connection.capabilities`

Protocol command and Unreal bridge skeleton entry. Planned MCP dispatch
wiring will return supported command names. The current bridge/protocol
shape is:

```json
{
  "commands": [
    "connection.ping",
    "connection.status",
    "connection.capabilities"
  ]
}
```
