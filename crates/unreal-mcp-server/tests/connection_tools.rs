use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use unreal_mcp_protocol::{Command, CommandResult, ErrorMode, RequestEnvelope, ResponseMode};
use unreal_mcp_server::{BridgeClient, ConnectionTools, ToolResponse};
use unreal_mcp_tests::start_fake_bridge;

const MAX_FRAME_BYTES: usize = 16 * 1024 * 1024;

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

#[tokio::test]
async fn connection_tools_return_compact_ping_response() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let response: ToolResponse = tools.ping().await.expect("ping response");

    assert_eq!(response.tool_name, "connection.ping");
    assert!(response.summary.contains("fake-0.1.0"));
}

#[tokio::test]
async fn bridge_client_rejects_oversized_response_frame() {
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .expect("bind oversized response bridge");
    let address = listener
        .local_addr()
        .expect("oversized response bridge address")
        .to_string();

    let bridge_task = tokio::spawn(async move {
        let (mut stream, _) = listener
            .accept()
            .await
            .expect("accept oversized response connection");
        let mut request_len = [0_u8; 4];
        stream
            .read_exact(&mut request_len)
            .await
            .expect("read request length");
        let request_len = u32::from_be_bytes(request_len) as usize;
        let mut request_body = vec![0_u8; request_len];
        stream
            .read_exact(&mut request_body)
            .await
            .expect("read request body");

        let oversized_len = (MAX_FRAME_BYTES as u32) + 1;
        stream
            .write_all(&oversized_len.to_be_bytes())
            .await
            .expect("write oversized response length");
    });

    let client = BridgeClient::new(address);
    let error = client
        .send(RequestEnvelope::new(
            1,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect_err("oversized response should fail");

    let error = format!("{error:#}");
    assert!(
        error.contains("frame too large"),
        "expected oversized frame error, got: {error}"
    );

    bridge_task.await.expect("oversized response bridge task");
}
