use tokio::io::{AsyncRead, AsyncWrite};

pub async fn run_stdio_server<R, W>(_reader: R, _writer: W) -> anyhow::Result<()>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    Ok(())
}
