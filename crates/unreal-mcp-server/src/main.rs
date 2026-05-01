#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            std::env::var("RUST_LOG").unwrap_or_else(|_| "unreal_mcp_server=info".to_string()),
        )
        .with_writer(std::io::stderr)
        .init();

    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();
    unreal_mcp_server::mcp_stdio::run_stdio_server(stdin, stdout).await
}
