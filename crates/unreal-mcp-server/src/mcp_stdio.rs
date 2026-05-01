use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWrite, AsyncWriteExt, BufReader, BufWriter};

use crate::{ConnectionTools, ToolResponse};

const MCP_PROTOCOL_VERSION: &str = "2025-03-26";
const SERVER_NAME: &str = "unreal-mcp-server";
const SERVER_VERSION: &str = "0.1.0";

pub async fn run_stdio_server<R, W>(
    reader: R,
    writer: W,
    tools: ConnectionTools,
) -> anyhow::Result<()>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let mut lines = BufReader::new(reader).lines();
    let mut writer = BufWriter::new(writer);

    while let Some(line) = lines.next_line().await? {
        if line.trim().is_empty() {
            continue;
        }

        let response = handle_line(&tools, &line).await;
        if let Some(response) = response {
            writer.write_all(response.to_string().as_bytes()).await?;
            writer.write_all(b"\n").await?;
            writer.flush().await?;
        }
    }

    Ok(())
}

async fn handle_line(tools: &ConnectionTools, line: &str) -> Option<Value> {
    let request = match serde_json::from_str::<Value>(line) {
        Ok(request) => request,
        Err(error) => {
            return Some(json_rpc_error(
                Value::Null,
                -32700,
                format!("Parse error: {error}"),
            ));
        }
    };

    let Some(id) = request.get("id").cloned() else {
        return None;
    };

    let Some(method) = request.get("method").and_then(Value::as_str) else {
        return Some(json_rpc_error(
            id,
            -32600,
            "Invalid request: missing method".to_string(),
        ));
    };

    Some(match method {
        "initialize" => initialize_response(id, &request),
        "tools/list" => tools_list_response(id),
        "tools/call" => tools_call_response(id, request.get("params"), tools).await,
        _ => json_rpc_error(id, -32601, format!("Method not found: {method}")),
    })
}

fn initialize_response(id: Value, request: &Value) -> Value {
    let requested_version = request
        .get("params")
        .and_then(|params| params.get("protocolVersion"))
        .and_then(Value::as_str)
        .unwrap_or(MCP_PROTOCOL_VERSION);
    let protocol_version = if requested_version == MCP_PROTOCOL_VERSION {
        requested_version
    } else {
        MCP_PROTOCOL_VERSION
    };

    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": {
            "protocolVersion": protocol_version,
            "capabilities": {
                "tools": {
                    "listChanged": false
                }
            },
            "serverInfo": {
                "name": SERVER_NAME,
                "version": SERVER_VERSION
            }
        }
    })
}

fn tools_list_response(id: Value) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": {
            "tools": [
                tool_definition(
                    "connection.ping",
                    "Check whether the Unreal bridge responds."
                ),
                tool_definition(
                    "connection.status",
                    "Return compact Unreal bridge connection status."
                ),
                tool_definition(
                    "connection.capabilities",
                    "Return bridge-supported command names."
                )
            ]
        }
    })
}

fn tool_definition(name: &str, description: &str) -> Value {
    json!({
        "name": name,
        "description": description,
        "inputSchema": {
            "type": "object",
            "properties": {},
            "additionalProperties": false
        }
    })
}

async fn tools_call_response(id: Value, params: Option<&Value>, tools: &ConnectionTools) -> Value {
    let Some(name) = params
        .and_then(|params| params.get("name"))
        .and_then(Value::as_str)
    else {
        return json_rpc_error(id, -32602, "Missing tool name".to_string());
    };

    match call_tool(name, tools).await {
        Ok(response) => json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "content": [
                    {
                        "type": "text",
                        "text": response.summary
                    },
                    {
                        "type": "text",
                        "text": response.data.to_string()
                    }
                ],
                "isError": false
            }
        }),
        Err(error) if error.to_string().starts_with("Unknown tool") => {
            json_rpc_error(id, -32602, error.to_string())
        }
        Err(error) => json!({
            "jsonrpc": "2.0",
            "id": id,
            "result": {
                "content": [
                    {
                        "type": "text",
                        "text": error.to_string()
                    }
                ],
                "isError": true
            }
        }),
    }
}

async fn call_tool(name: &str, tools: &ConnectionTools) -> anyhow::Result<ToolResponse> {
    match name {
        "connection.ping" => tools.ping().await,
        "connection.status" => tools.status().await,
        "connection.capabilities" => tools.capabilities().await,
        _ => anyhow::bail!("Unknown tool: {name}"),
    }
}

fn json_rpc_error(id: Value, code: i64, message: String) -> Value {
    json!({
        "jsonrpc": "2.0",
        "id": id,
        "error": {
            "code": code,
            "message": message
        }
    })
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use crate::{BridgeClient, ConnectionTools};
    use unreal_mcp_tests::start_fake_bridge;

    use super::run_stdio_server;
    use tokio::time::timeout;

    #[tokio::test]
    async fn run_stdio_server_waits_for_stdin_eof() {
        let fake = start_fake_bridge().await.expect("fake bridge");
        let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));
        let (input_writer, server_reader) = tokio::io::duplex(64);
        let (_output_reader, server_writer) = tokio::io::duplex(64);

        let mut server_task = tokio::spawn(run_stdio_server(server_reader, server_writer, tools));

        let early_exit = timeout(Duration::from_millis(25), &mut server_task).await;
        assert!(
            early_exit.is_err(),
            "stdio server should not complete before stdin EOF"
        );

        drop(input_writer);

        let result = timeout(Duration::from_secs(1), server_task)
            .await
            .expect("stdio server should exit after stdin EOF")
            .expect("stdio server task should not panic");
        result.expect("stdio server should return ok");
    }
}
