use unreal_mcp_protocol::{Command, CommandResult, ErrorMode, RequestEnvelope, ResponseMode};
use unreal_mcp_server::BridgeClient;
use unreal_mcp_tests::start_fake_bridge;

#[tokio::test]
async fn bridge_client_sends_ping_and_receives_pong() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let client = BridgeClient::new(fake.address().to_string());

    let response = client
        .send(RequestEnvelope::new(
            1,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect("bridge response");

    assert!(response.ok);
    assert_eq!(response.results.len(), 1);
    assert!(matches!(
        &response.results[0],
        CommandResult::Pong { bridge_version } if bridge_version == "fake-0.1.0"
    ));
}
