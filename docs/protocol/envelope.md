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
