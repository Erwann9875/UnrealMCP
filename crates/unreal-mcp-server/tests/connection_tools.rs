use std::time::Duration;

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::task::JoinHandle;
use tokio::time::sleep;
use unreal_mcp_protocol::{
    decode_json_request, encode_json_response, encode_msgpack_response, BridgeStatus, Command,
    CommandResult, ErrorMode, RequestEnvelope, ResponseEnvelope, ResponseMode,
};
use unreal_mcp_server::{BridgeClient, BridgeFormat, ConnectionTools, ToolResponse};
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
async fn bridge_client_sends_json_ping_and_receives_pong() {
    let (address, bridge_task) = start_single_json_response_bridge().await.expect("json bridge");
    let client = BridgeClient::with_format(address, BridgeFormat::Json);

    let response = client
        .send(RequestEnvelope::new(
            77,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect("json bridge response");

    assert!(response.ok);
    assert_eq!(response.request_id, 77);
    assert!(matches!(
        &response.results[0],
        CommandResult::Pong { bridge_version } if bridge_version == "json-fake-0.1.0"
    ));

    bridge_task.await.expect("json bridge task");
}

#[tokio::test]
async fn connection_tools_return_compact_ping_response() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let response: ToolResponse = tools.ping().await.expect("ping response");

    assert_eq!(response.tool_name, "connection.ping");
    assert!(response.summary.contains("fake-0.1.0"));
    assert_eq!(response.data["ok"].as_bool(), Some(true));
    assert_eq!(response.data["bridge_version"].as_str(), Some("fake-0.1.0"));
    assert!(response.data["elapsed_ms"].is_number());
}

#[tokio::test]
async fn connection_tools_return_compact_status_response() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let response: ToolResponse = tools.status().await.expect("status response");

    assert_eq!(response.tool_name, "connection.status");
    assert!(response.summary.contains("connected"));
    assert_eq!(response.data["connected"].as_bool(), Some(true));
    assert_eq!(response.data["bridge_version"].as_str(), Some("fake-0.1.0"));
    assert_eq!(
        response.data["unreal_version"].as_str(),
        Some("fake-unreal")
    );
    assert!(response.data["elapsed_ms"].is_number());
}

#[tokio::test]
async fn connection_tools_return_compact_capabilities_response() {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let response: ToolResponse = tools.capabilities().await.expect("capabilities response");

    assert_eq!(response.tool_name, "connection.capabilities");
    assert!(response.summary.contains("3 commands"));
    assert_eq!(
        response.data["commands"]
            .as_array()
            .expect("commands should be an array")
            .len(),
        3
    );
    assert_eq!(
        response.data["commands"][0].as_str(),
        Some("connection.ping")
    );
}

#[tokio::test]
async fn connection_tools_reject_unexpected_ping_result() {
    let (address, bridge_task) = start_single_response_bridge(ResponseEnvelope::success(
        1,
        3,
        vec![CommandResult::Status(BridgeStatus {
            connected: true,
            bridge_version: Some("fake-0.1.0".to_string()),
            unreal_version: Some("fake-unreal".to_string()),
        })],
    ))
    .await
    .expect("single response bridge");

    let tools = ConnectionTools::new(BridgeClient::new(address));

    let error = tools
        .ping()
        .await
        .expect_err("unexpected ping result should fail");
    let error = format!("{error:#}");
    assert!(
        error.contains("unexpected ping response"),
        "expected unexpected ping response error, got: {error}"
    );

    bridge_task.await.expect("single response bridge task");
}

#[tokio::test]
async fn connection_tools_reject_extra_ping_result() {
    let (address, bridge_task) = start_single_response_bridge(ResponseEnvelope::success(
        1,
        3,
        vec![
            CommandResult::Pong {
                bridge_version: "fake-0.1.0".to_string(),
            },
            CommandResult::Status(BridgeStatus {
                connected: true,
                bridge_version: Some("fake-0.1.0".to_string()),
                unreal_version: Some("fake-unreal".to_string()),
            }),
        ],
    ))
    .await
    .expect("single response bridge");

    let tools = ConnectionTools::new(BridgeClient::new(address));

    let error = tools
        .ping()
        .await
        .expect_err("extra ping result should fail");
    let error = format!("{error:#}");
    assert!(
        error.contains("unexpected ping response"),
        "expected unexpected ping response error, got: {error}"
    );

    bridge_task.await.expect("single response bridge task");
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

#[tokio::test]
async fn bridge_client_rejects_mismatched_response_request_id() {
    let (address, bridge_task) = start_single_response_bridge(ResponseEnvelope::success(
        999,
        3,
        vec![CommandResult::Pong {
            bridge_version: "fake-0.1.0".to_string(),
        }],
    ))
    .await
    .expect("single response bridge");

    let client = BridgeClient::new(address);
    let error = client
        .send(RequestEnvelope::new(
            42,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect_err("mismatched response request id should fail");

    let error = format!("{error:#}");
    assert!(
        error.contains("request id mismatch"),
        "expected request id mismatch error, got: {error}"
    );

    bridge_task.await.expect("single response bridge task");
}

#[tokio::test]
async fn bridge_client_times_out_when_bridge_stalls() {
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .expect("bind stalled bridge");
    let address = listener
        .local_addr()
        .expect("stalled bridge address")
        .to_string();

    let bridge_task = tokio::spawn(async move {
        let (_stream, _) = listener.accept().await.expect("accept stalled connection");
        sleep(Duration::from_secs(1)).await;
    });

    let client = BridgeClient::with_timeout(address, Duration::from_millis(50));
    let error = client
        .send(RequestEnvelope::new(
            42,
            ResponseMode::Summary,
            ErrorMode::Stop,
            vec![Command::Ping],
        ))
        .await
        .expect_err("stalled bridge should time out");

    let error = format!("{error:#}");
    assert!(
        error.contains("timed out"),
        "expected timed out error, got: {error}"
    );

    bridge_task.abort();
    let _ = bridge_task.await;
}

async fn start_single_json_response_bridge() -> anyhow::Result<(String, JoinHandle<()>)> {
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let address = listener.local_addr()?.to_string();

    let bridge_task = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.expect("accept json connection");
        let mut request_len = [0_u8; 4];
        stream
            .read_exact(&mut request_len)
            .await
            .expect("read json request length");
        let request_len = u32::from_be_bytes(request_len) as usize;
        let mut request_body = vec![0_u8; request_len];
        stream
            .read_exact(&mut request_body)
            .await
            .expect("read json request body");

        let request_text = std::str::from_utf8(&request_body).expect("request is UTF-8");
        let request = decode_json_request(request_text).expect("decode json request");
        assert_eq!(request.commands, vec![Command::Ping]);

        let response = ResponseEnvelope::success(
            request.request_id,
            2,
            vec![CommandResult::Pong {
                bridge_version: "json-fake-0.1.0".to_string(),
            }],
        );
        let response_body = encode_json_response(&response)
            .expect("encode json response")
            .into_bytes();
        stream
            .write_all(&(response_body.len() as u32).to_be_bytes())
            .await
            .expect("write json response length");
        stream
            .write_all(&response_body)
            .await
            .expect("write json response body");
    });

    Ok((address, bridge_task))
}

async fn start_single_response_bridge(
    response: ResponseEnvelope,
) -> anyhow::Result<(String, JoinHandle<()>)> {
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let address = listener.local_addr()?.to_string();

    let bridge_task = tokio::spawn(async move {
        let (mut stream, _) = listener.accept().await.expect("accept connection");
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

        let response_body = encode_msgpack_response(&response).expect("encode response");
        stream
            .write_all(&(response_body.len() as u32).to_be_bytes())
            .await
            .expect("write response length");
        stream
            .write_all(&response_body)
            .await
            .expect("write response body");
    });

    Ok((address, bridge_task))
}
