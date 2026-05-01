use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite};

pub async fn run_stdio_server<R, W>(mut reader: R, _writer: W) -> anyhow::Result<()>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    let mut buffer = [0_u8; 8192];
    loop {
        if reader.read(&mut buffer).await? == 0 {
            return Ok(());
        }
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::run_stdio_server;
    use tokio::time::timeout;

    #[tokio::test]
    async fn run_stdio_server_waits_for_stdin_eof() {
        let (input_writer, server_reader) = tokio::io::duplex(64);
        let (_output_reader, server_writer) = tokio::io::duplex(64);

        let mut server_task = tokio::spawn(run_stdio_server(server_reader, server_writer));

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
