use serde_json::{json, Value};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use unreal_mcp_server::{mcp_stdio::run_stdio_server, BridgeClient, ConnectionTools};
use unreal_mcp_tests::start_fake_bridge;

#[tokio::test]
async fn stdio_initialize_returns_tools_capability() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {
                "name": "test-client",
                "version": "0.1.0"
            }
        }
    }))
    .await;

    assert_eq!(response["jsonrpc"], "2.0");
    assert_eq!(response["id"], 1);
    assert_eq!(response["result"]["protocolVersion"], "2025-03-26");
    assert!(response["result"]["capabilities"]["tools"].is_object());
    assert_eq!(
        response["result"]["serverInfo"]["name"],
        "unreal-mcp-server"
    );
}

#[tokio::test]
async fn stdio_tools_list_returns_connection_level_and_world_tools() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": "tools",
        "method": "tools/list"
    }))
    .await;

    let tools = response["result"]["tools"]
        .as_array()
        .expect("tools should be an array");
    let names: Vec<_> = tools
        .iter()
        .map(|tool| tool["name"].as_str().expect("tool name"))
        .collect();

    assert_eq!(
        names,
        vec![
            "connection.ping",
            "connection.status",
            "connection.capabilities",
            "level.create",
            "level.open",
            "level.save",
            "level.list",
            "world.bulk_spawn",
            "world.bulk_delete",
            "world.query",
            "world.snapshot",
            "asset.create_folder",
            "material.create",
            "material.create_instance",
            "material.create_procedural_texture",
            "material.set_parameters",
            "material.bulk_apply",
            "world.bulk_set_materials",
        ]
    );
    assert_eq!(tools[0]["inputSchema"]["type"], "object");
    for tool in tools {
        let schema = &tool["inputSchema"];
        assert_eq!(
            schema["type"], "object",
            "tool {} must use an object schema",
            tool["name"]
        );
        for disallowed_keyword in ["anyOf", "oneOf", "allOf", "not"] {
            assert!(
                schema.get(disallowed_keyword).is_none(),
                "tool {} must not use top-level {disallowed_keyword}",
                tool["name"]
            );
        }
    }

    let tool_by_name = |name: &str| {
        tools
            .iter()
            .find(|tool| tool["name"] == name)
            .unwrap_or_else(|| panic!("missing tool {name}"))
    };
    assert_eq!(
        tool_by_name("level.create")["inputSchema"]["required"],
        json!(["path"])
    );
    assert_eq!(
        tool_by_name("level.open")["inputSchema"]["required"],
        json!(["path"])
    );
    assert_eq!(
        tool_by_name("world.bulk_spawn")["inputSchema"]["required"],
        json!(["actors"])
    );
    let actor_schema =
        &tool_by_name("world.bulk_spawn")["inputSchema"]["properties"]["actors"]["items"];
    assert_eq!(actor_schema["required"], json!(["name", "mesh"]));
    assert_eq!(actor_schema["properties"]["location"]["maxItems"], 3);
    assert!(tool_by_name("world.bulk_delete")["inputSchema"]["properties"]["names"].is_object());
    assert!(tool_by_name("world.bulk_delete")["inputSchema"]["properties"]["tags"].is_object());
    assert_eq!(
        tool_by_name("asset.create_folder")["inputSchema"]["required"],
        json!(["path"])
    );
    assert_eq!(
        tool_by_name("material.create")["inputSchema"]["required"],
        json!(["path"])
    );
    assert_eq!(
        tool_by_name("material.bulk_apply")["inputSchema"]["required"],
        json!(["assignments"])
    );
    assert_eq!(
        tool_by_name("world.bulk_set_materials")["inputSchema"]["required"],
        json!(["material"])
    );
}

#[tokio::test]
async fn stdio_tools_call_dispatches_connection_ping() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "connection.ping",
            "arguments": {}
        }
    }))
    .await;

    assert_eq!(response["jsonrpc"], "2.0");
    assert_eq!(response["id"], 2);
    assert_eq!(response["result"]["isError"], false);

    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("fake-0.1.0"));
}

#[tokio::test]
async fn stdio_tools_call_dispatches_level_list() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 4,
        "method": "tools/call",
        "params": {
            "name": "level.list",
            "arguments": {}
        }
    }))
    .await;

    assert_eq!(response["result"]["isError"], false);
    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("1 level"));
    assert!(content[1]["text"]
        .as_str()
        .expect("data text")
        .contains("/Game/MCP/Generated/L_Fake"));
}

#[tokio::test]
async fn stdio_tools_call_dispatches_world_bulk_spawn() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 5,
        "method": "tools/call",
        "params": {
            "name": "world.bulk_spawn",
            "arguments": {
                "actors": [
                    {
                        "name": "MCP_Test_Cube",
                        "mesh": "/Engine/BasicShapes/Cube.Cube",
                        "location": [0.0, 0.0, 50.0],
                        "rotation": [0.0, 0.0, 0.0],
                        "scale": [1.0, 1.0, 1.0],
                        "scene": "test_scene",
                        "group": "test_group"
                    }
                ]
            }
        }
    }))
    .await;

    assert_eq!(response["result"]["isError"], false);
    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("Spawned 1 actor"));
}

#[tokio::test]
async fn stdio_tools_call_dispatches_world_query() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 6,
        "method": "tools/call",
        "params": {
            "name": "world.query",
            "arguments": {
                "tags": ["mcp.generated"],
                "include_generated": true,
                "limit": 10
            }
        }
    }))
    .await;

    assert_eq!(response["result"]["isError"], false);
    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("1 actor"));
}

#[tokio::test]
async fn stdio_tools_call_dispatches_material_create() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 7,
        "method": "tools/call",
        "params": {
            "name": "material.create",
            "arguments": {
                "path": "/Game/MCP/Materials/M_TestConcrete",
                "base_color": [0.45, 0.45, 0.42, 1.0],
                "roughness": 0.8
            }
        }
    }))
    .await;

    assert_eq!(response["result"]["isError"], false);
    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("Created material"));
    assert!(content[1]["text"]
        .as_str()
        .expect("data text")
        .contains("/Game/MCP/Materials/M_TestConcrete"));
}

#[tokio::test]
async fn stdio_tools_call_dispatches_material_bulk_apply() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 8,
        "method": "tools/call",
        "params": {
            "name": "material.bulk_apply",
            "arguments": {
                "assignments": [
                    {
                        "material": "/Game/MCP/Materials/M_TestConcrete",
                        "tags": ["mcp.group:buildings"],
                        "slot": 0
                    }
                ]
            }
        }
    }))
    .await;

    assert_eq!(response["result"]["isError"], false);
    let content = response["result"]["content"]
        .as_array()
        .expect("content should be an array");
    assert!(content[0]["text"]
        .as_str()
        .expect("summary text")
        .contains("Applied material to 1 actor"));
}

#[tokio::test]
async fn stdio_tools_call_rejects_unknown_tool() {
    let response = run_single_request(json!({
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "connection.missing",
            "arguments": {}
        }
    }))
    .await;

    assert_eq!(response["jsonrpc"], "2.0");
    assert_eq!(response["id"], 3);
    assert_eq!(response["error"]["code"], -32602);
    assert!(response["error"]["message"]
        .as_str()
        .expect("error message")
        .contains("Unknown tool"));
}

#[tokio::test]
async fn stdio_initialized_notification_produces_no_response() {
    let output = run_messages(vec![json!({
        "jsonrpc": "2.0",
        "method": "notifications/initialized"
    })])
    .await;

    assert!(output.trim().is_empty());
}

async fn run_single_request(request: Value) -> Value {
    let output = run_messages(vec![request]).await;
    let first_line = output.lines().next().expect("one response line");
    serde_json::from_str(first_line).expect("valid json-rpc response")
}

async fn run_messages(messages: Vec<Value>) -> String {
    let fake = start_fake_bridge().await.expect("fake bridge");
    let tools = ConnectionTools::new(BridgeClient::new(fake.address().to_string()));

    let (mut input_writer, server_reader) = tokio::io::duplex(4096);
    let (server_writer, mut output_reader) = tokio::io::duplex(8192);

    let server_task = tokio::spawn(run_stdio_server(server_reader, server_writer, tools));

    for message in messages {
        input_writer
            .write_all(message.to_string().as_bytes())
            .await
            .expect("write request");
        input_writer.write_all(b"\n").await.expect("write newline");
    }
    drop(input_writer);

    let mut output = String::new();
    output_reader
        .read_to_string(&mut output)
        .await
        .expect("read response");

    server_task
        .await
        .expect("stdio server task should not panic")
        .expect("stdio server should return ok");

    output
}
