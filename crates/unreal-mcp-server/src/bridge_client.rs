use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

use unreal_mcp_protocol::{
    decode_msgpack_response, encode_msgpack_request, RequestEnvelope, ResponseEnvelope,
};

#[derive(Debug, Clone)]
pub struct BridgeClient {
    address: String,
}

impl BridgeClient {
    pub fn new(address: String) -> Self {
        Self { address }
    }

    pub fn address(&self) -> &str {
        &self.address
    }

    pub async fn send(&self, request: RequestEnvelope) -> anyhow::Result<ResponseEnvelope> {
        let mut stream = TcpStream::connect(&self.address).await?;
        let body = encode_msgpack_request(&request)?;
        stream.write_all(&(body.len() as u32).to_be_bytes()).await?;
        stream.write_all(&body).await?;

        let mut length_buf = [0_u8; 4];
        stream.read_exact(&mut length_buf).await?;
        let length = u32::from_be_bytes(length_buf) as usize;
        let mut response_body = vec![0_u8; length];
        stream.read_exact(&mut response_body).await?;

        Ok(decode_msgpack_response(&response_body)?)
    }
}
