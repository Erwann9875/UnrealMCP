use serde_json::json;

use unreal_mcp_protocol::{Command, CommandResult, ErrorMode, RequestEnvelope, ResponseMode};

use crate::BridgeClient;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolResponse {
    pub tool_name: &'static str,
    pub summary: String,
    pub data: serde_json::Value,
}

#[derive(Debug, Clone)]
pub struct ConnectionTools {
    bridge: BridgeClient,
}

impl ConnectionTools {
    pub fn new(bridge: BridgeClient) -> Self {
        Self { bridge }
    }

    pub async fn ping(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                1,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Ping],
            ))
            .await?;

        let bridge_version = match response.results.first() {
            Some(CommandResult::Pong { bridge_version }) => bridge_version.clone(),
            _ => "unknown".to_string(),
        };

        Ok(ToolResponse {
            tool_name: "connection.ping",
            summary: format!("Unreal bridge responded with version {bridge_version}."),
            data: json!({
                "ok": response.ok,
                "bridge_version": bridge_version,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }
}
