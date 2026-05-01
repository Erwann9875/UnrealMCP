#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            std::env::var("RUST_LOG").unwrap_or_else(|_| "unreal_mcp_server=info".to_string()),
        )
        .with_writer(std::io::stderr)
        .init();

    let bridge_address =
        std::env::var("UNREAL_MCP_BRIDGE_ADDR").unwrap_or_else(|_| "127.0.0.1:55557".to_string());
    let bridge_format = std::env::var("UNREAL_MCP_BRIDGE_FORMAT")
        .map(|value| unreal_mcp_server::BridgeFormat::parse(&value))
        .unwrap_or(Ok(unreal_mcp_server::BridgeFormat::Msgpack))?;
    let tools = unreal_mcp_server::ConnectionTools::new(
        unreal_mcp_server::BridgeClient::with_format(bridge_address, bridge_format),
    );
    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();
    unreal_mcp_server::mcp_stdio::run_stdio_server(stdin, stdout, tools).await
}
