# MCP Tools

This foundation defines the initial `connection` protocol/tool surface.
Only `connection.ping` has a Rust tool handler in this foundation. The
stdio MCP dispatch path is still a lifetime skeleton; MCP JSON-RPC dispatch
and stdio tool registration are deferred to a follow-up plan.

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
