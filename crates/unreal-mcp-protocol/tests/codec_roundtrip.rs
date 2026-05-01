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
