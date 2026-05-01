use unreal_mcp_protocol::{
    Command, CommandResult, ErrorMode, RequestEnvelope, ResponseEnvelope, ResponseMode,
};

#[test]
fn request_envelope_keeps_bulk_commands_ordered() {
    let envelope = RequestEnvelope::new(
        7,
        ResponseMode::Handles,
        ErrorMode::Continue,
        vec![Command::Ping, Command::Capabilities],
    );

    assert_eq!(envelope.protocol_version, 1);
    assert_eq!(envelope.request_id, 7);
    assert_eq!(envelope.commands.len(), 2);
    assert!(matches!(envelope.commands[0], Command::Ping));
    assert!(matches!(envelope.commands[1], Command::Capabilities));
}

#[test]
fn response_envelope_can_return_compact_success() {
    let response = ResponseEnvelope::success(
        7,
        3,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );

    assert!(response.ok);
    assert_eq!(response.request_id, 7);
    assert_eq!(response.elapsed_ms, 3);
    assert_eq!(response.errors.len(), 0);
    assert_eq!(response.results.len(), 1);
}

use unreal_mcp_protocol::{decode_json_request, encode_json_request};
use unreal_mcp_protocol::{decode_msgpack_request, encode_msgpack_request};

#[test]
fn msgpack_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        42,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::Ping],
    );

    let bytes = encode_msgpack_request(&request).expect("encode request");
    let decoded = decode_msgpack_request(&bytes).expect("decode request");

    assert_eq!(decoded, request);
}

#[test]
fn json_debug_request_roundtrip_preserves_envelope() {
    let request = RequestEnvelope::new(
        43,
        ResponseMode::Full,
        ErrorMode::Continue,
        vec![Command::Status],
    );

    let text = encode_json_request(&request).expect("encode request");
    let decoded = decode_json_request(&text).expect("decode request");

    assert_eq!(decoded, request);
}
