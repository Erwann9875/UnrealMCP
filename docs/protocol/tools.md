# MCP Tools

The first implemented tool group is `connection`.

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

## `connection.capabilities`

Returns supported command names. The first version returns:

```json
[
  "connection.ping",
  "connection.status",
  "connection.capabilities"
]
```
