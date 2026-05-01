# Unreal JSON Bridge Bootstrap Design

## Purpose

The next slice makes the Rust MCP server talk to the Unreal Editor plugin end to end. The bridge will support the existing connection tools first: `connection.ping`, `connection.status`, and `connection.capabilities`.

## Approach

The optimized protocol remains length-prefixed MessagePack. For this bootstrap slice, the same length-prefixed frame is reused with a JSON body so the Unreal plugin can implement the protocol without pulling in a MessagePack dependency yet. Rust selects the bridge body format with `UNREAL_MCP_BRIDGE_FORMAT=json|msgpack`; the default stays `msgpack` for tests and future performance work.

## Components

- Rust `BridgeClient`: keeps the existing TCP framing, adds a `BridgeFormat` enum, and encodes/decodes request and response envelopes as either MessagePack or JSON.
- Unreal `FBridgeServer`: binds `127.0.0.1:55557`, accepts short-lived TCP connections on a worker thread, reads one length-prefixed JSON request, and writes one length-prefixed JSON response.
- Unreal command handling: parses the connection commands in the request envelope and returns protocol-compatible JSON response envelopes with compact results.
- `MCPTester` install: copies the source plugin into `MCPTester/Plugins/UnrealMCPBridge` and enables it in `MCPTester.uproject`.

## Error Handling

Rust keeps request timeout, frame size, and request-id mismatch checks. Unreal rejects malformed frames, unsupported protocol versions, oversized frames, and unknown command types by returning a failure envelope when it can identify the request id; otherwise it closes the connection after logging the issue.

## Testing

Rust tests cover JSON bridge encoding over the same TCP framing, environment parsing for bridge format, mismatched request ids, and the existing MCP stdio dispatch. Unreal is verified by compiling the plugin against the `MCPTester` project when the Unreal build tool is available.
