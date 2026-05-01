use serde_json::json;

use anyhow::{bail, ensure};
use unreal_mcp_protocol::{
    BridgeStatus, Command, CommandResult, ErrorMode, RequestEnvelope, ResponseEnvelope,
    ResponseMode,
};

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

        ensure_bridge_success("ping", &response)?;

        let bridge_version = match response.results.as_slice() {
            [CommandResult::Pong { bridge_version }] => bridge_version.clone(),
            [] => bail!("unexpected ping response: missing pong result"),
            [result] => bail!("unexpected ping response: expected pong, got {result:?}"),
            results => bail!(
                "unexpected ping response: expected exactly one pong result, got {} results",
                results.len()
            ),
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

    pub async fn status(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                2,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Status],
            ))
            .await?;

        ensure_bridge_success("status", &response)?;

        let status = match response.results.as_slice() {
            [CommandResult::Status(status)] => status.clone(),
            [] => bail!("unexpected status response: missing status result"),
            [result] => bail!("unexpected status response: expected status, got {result:?}"),
            results => bail!(
                "unexpected status response: expected exactly one status result, got {} results",
                results.len()
            ),
        };

        Ok(status_response(response.elapsed_ms, status))
    }

    pub async fn capabilities(&self) -> anyhow::Result<ToolResponse> {
        let response = self
            .bridge
            .send(RequestEnvelope::new(
                3,
                ResponseMode::Summary,
                ErrorMode::Stop,
                vec![Command::Capabilities],
            ))
            .await?;

        ensure_bridge_success("capabilities", &response)?;

        let commands = match response.results.as_slice() {
            [CommandResult::Capabilities { commands }] => commands.clone(),
            [] => bail!("unexpected capabilities response: missing capabilities result"),
            [result] => bail!(
                "unexpected capabilities response: expected capabilities, got {result:?}"
            ),
            results => bail!(
                "unexpected capabilities response: expected exactly one capabilities result, got {} results",
                results.len()
            ),
        };

        Ok(ToolResponse {
            tool_name: "connection.capabilities",
            summary: format!("Unreal bridge reported {} commands.", commands.len()),
            data: json!({
                "commands": commands,
                "elapsed_ms": response.elapsed_ms
            }),
        })
    }
}

fn ensure_bridge_success(command_name: &str, response: &ResponseEnvelope) -> anyhow::Result<()> {
    ensure!(response.ok, "bridge {command_name} response was not ok");
    ensure!(
        response.errors.is_empty(),
        "bridge {command_name} response included errors: {:?}",
        response.errors
    );
    Ok(())
}

fn status_response(elapsed_ms: u32, status: BridgeStatus) -> ToolResponse {
    let state = if status.connected {
        "connected"
    } else {
        "disconnected"
    };

    ToolResponse {
        tool_name: "connection.status",
        summary: format!("Unreal bridge is {state}."),
        data: json!({
            "connected": status.connected,
            "bridge_version": status.bridge_version,
            "unreal_version": status.unreal_version,
            "elapsed_ms": elapsed_ms
        }),
    }
}
