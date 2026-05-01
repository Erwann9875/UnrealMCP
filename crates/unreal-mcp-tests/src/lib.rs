use anyhow::Context;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

use unreal_mcp_protocol::{
    decode_msgpack_request, encode_msgpack_response, Command, CommandResult, ResponseEnvelope,
};

pub struct FakeBridge {
    address: String,
}

impl FakeBridge {
    pub fn address(&self) -> &str {
        &self.address
    }
}

pub async fn start_fake_bridge() -> anyhow::Result<FakeBridge> {
    let listener = TcpListener::bind("127.0.0.1:0")
        .await
        .context("bind fake bridge")?;
    let address = listener.local_addr()?.to_string();

    tokio::spawn(async move {
        loop {
            let Ok((stream, _)) = listener.accept().await else {
                break;
            };
            tokio::spawn(handle_connection(stream));
        }
    });

    Ok(FakeBridge { address })
}

async fn handle_connection(mut stream: TcpStream) -> anyhow::Result<()> {
    let mut length_buf = [0_u8; 4];
    stream.read_exact(&mut length_buf).await?;
    let length = u32::from_be_bytes(length_buf) as usize;
    let mut body = vec![0_u8; length];
    stream.read_exact(&mut body).await?;

    let request = decode_msgpack_request(&body)?;
    let results = request
        .commands
        .into_iter()
        .map(|command| match command {
            Command::Ping => CommandResult::Pong {
                bridge_version: "fake-0.1.0".to_string(),
            },
            Command::Status => CommandResult::Status(unreal_mcp_protocol::BridgeStatus {
                connected: true,
                bridge_version: Some("fake-0.1.0".to_string()),
                unreal_version: Some("fake-unreal".to_string()),
            }),
            Command::Capabilities => CommandResult::Capabilities {
                commands: vec![
                    "connection.ping".to_string(),
                    "connection.status".to_string(),
                    "connection.capabilities".to_string(),
                ],
            },
        })
        .collect();

    let response = ResponseEnvelope::success(request.request_id, 1, results);
    let response_body = encode_msgpack_response(&response)?;
    stream
        .write_all(&(response_body.len() as u32).to_be_bytes())
        .await?;
    stream.write_all(&response_body).await?;
    Ok(())
}
