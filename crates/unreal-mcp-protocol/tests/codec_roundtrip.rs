use unreal_mcp_protocol::{
    Command, CommandResult, ErrorMode, IndexedError, ProtocolError, RequestEnvelope,
    ResponseEnvelope, ResponseMode,
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

use unreal_mcp_protocol::{
    decode_json_request, decode_json_response, decode_msgpack_request, decode_msgpack_response,
    encode_json_request, encode_json_response, encode_msgpack_request, encode_msgpack_response,
};

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

#[test]
fn msgpack_response_roundtrip_preserves_success_envelope() {
    let response = ResponseEnvelope::success(
        44,
        5,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );

    let bytes = encode_msgpack_response(&response).expect("encode response");
    let decoded = decode_msgpack_response(&bytes).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn json_debug_response_roundtrip_preserves_failure_envelope() {
    let response = ResponseEnvelope::failure(
        45,
        8,
        vec![IndexedError {
            command_index: 2,
            item_index: Some(3),
            code: "bridge_error".to_string(),
            message: "bridge unavailable".to_string(),
        }],
    );

    let text = encode_json_response(&response).expect("encode response");
    let decoded = decode_json_response(&text).expect("decode response");

    assert_eq!(decoded, response);
}

#[test]
fn decoders_reject_unsupported_protocol_version() {
    let expected = 1;
    let actual = 999;

    let mut request = RequestEnvelope::new(
        46,
        ResponseMode::Summary,
        ErrorMode::Stop,
        vec![Command::Ping],
    );
    request.protocol_version = actual;

    let request_json = encode_json_request(&request).expect("encode request");
    assert_unsupported_version(decode_json_request(&request_json), actual, expected);

    let request_msgpack = encode_msgpack_request(&request).expect("encode request");
    assert_unsupported_version(decode_msgpack_request(&request_msgpack), actual, expected);

    let mut response = ResponseEnvelope::success(
        46,
        13,
        vec![CommandResult::Pong {
            bridge_version: "0.1.0".to_string(),
        }],
    );
    response.protocol_version = actual;

    let response_json = encode_json_response(&response).expect("encode response");
    assert_unsupported_version(decode_json_response(&response_json), actual, expected);

    let response_msgpack = encode_msgpack_response(&response).expect("encode response");
    assert_unsupported_version(decode_msgpack_response(&response_msgpack), actual, expected);
}

fn assert_unsupported_version<T>(result: Result<T, ProtocolError>, actual: u16, expected: u16) {
    match result {
        Err(ProtocolError::UnsupportedProtocolVersion {
            actual: decoded,
            expected: supported,
        }) => {
            assert_eq!(decoded, actual);
            assert_eq!(supported, expected);
        }
        Err(error) => panic!("expected unsupported protocol version, got {error}"),
        Ok(_) => panic!("expected unsupported protocol version"),
    }
}
