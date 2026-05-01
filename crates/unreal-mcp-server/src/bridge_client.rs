use std::time::Duration;

use anyhow::{anyhow, bail, ensure, Context};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::time::timeout;

use unreal_mcp_protocol::{
    decode_json_response, decode_msgpack_response, encode_json_request, encode_msgpack_request,
    RequestEnvelope, ResponseEnvelope,
};

const MAX_FRAME_BYTES: usize = 16 * 1024 * 1024;
const DEFAULT_REQUEST_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BridgeFormat {
    Msgpack,
    Json,
}

impl BridgeFormat {
    pub fn parse(value: &str) -> anyhow::Result<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "msgpack" => Ok(Self::Msgpack),
            "json" => Ok(Self::Json),
            other => bail!("unsupported bridge format '{other}', expected 'msgpack' or 'json'"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct BridgeClient {
    address: String,
    request_timeout: Duration,
    format: BridgeFormat,
}

#[cfg(test)]
mod tests {
    use super::BridgeFormat;

    #[test]
    fn bridge_format_parses_supported_values() {
        assert_eq!(BridgeFormat::parse("msgpack").expect("msgpack"), BridgeFormat::Msgpack);
        assert_eq!(BridgeFormat::parse("json").expect("json"), BridgeFormat::Json);
    }

    #[test]
    fn bridge_format_parse_is_case_insensitive_and_trims_input() {
        assert_eq!(BridgeFormat::parse(" JSON ").expect("json"), BridgeFormat::Json);
        assert_eq!(
            BridgeFormat::parse(" MsgPack ").expect("msgpack"),
            BridgeFormat::Msgpack
        );
    }

    #[test]
    fn bridge_format_rejects_unknown_values() {
        let error = BridgeFormat::parse("xml").expect_err("xml should be rejected");
        assert!(
            error.to_string().contains("unsupported bridge format"),
            "expected unsupported bridge format error, got: {error:#}"
        );
    }
}

impl BridgeClient {
    pub fn new(address: String) -> Self {
        Self::with_timeout(address, DEFAULT_REQUEST_TIMEOUT)
    }

    pub fn with_timeout(address: String, request_timeout: Duration) -> Self {
        Self::with_timeout_and_format(address, request_timeout, BridgeFormat::Msgpack)
    }

    pub fn with_format(address: String, format: BridgeFormat) -> Self {
        Self::with_timeout_and_format(address, DEFAULT_REQUEST_TIMEOUT, format)
    }

    pub fn with_timeout_and_format(
        address: String,
        request_timeout: Duration,
        format: BridgeFormat,
    ) -> Self {
        Self {
            address,
            request_timeout,
            format,
        }
    }

    pub fn address(&self) -> &str {
        &self.address
    }

    pub async fn send(&self, request: RequestEnvelope) -> anyhow::Result<ResponseEnvelope> {
        let expected_request_id = request.request_id;
        let response = timeout(self.request_timeout, self.send_once(request))
            .await
            .map_err(|_| anyhow!("bridge request timed out after {:?}", self.request_timeout))??;

        ensure!(
            response.request_id == expected_request_id,
            "bridge response request id mismatch: expected {}, got {}",
            expected_request_id,
            response.request_id
        );

        Ok(response)
    }

    async fn send_once(&self, request: RequestEnvelope) -> anyhow::Result<ResponseEnvelope> {
        let mut stream = TcpStream::connect(&self.address).await?;
        let body = self.encode_request(&request)?;
        ensure!(
            body.len() <= MAX_FRAME_BYTES,
            "bridge request frame too large: {} bytes exceeds {} bytes",
            body.len(),
            MAX_FRAME_BYTES
        );
        let body_len = u32::try_from(body.len())?;
        stream.write_all(&body_len.to_be_bytes()).await?;
        stream.write_all(&body).await?;

        let mut length_buf = [0_u8; 4];
        stream.read_exact(&mut length_buf).await?;
        let length = u32::from_be_bytes(length_buf) as usize;
        ensure!(
            length <= MAX_FRAME_BYTES,
            "bridge response frame too large: {length} bytes exceeds {MAX_FRAME_BYTES} bytes"
        );
        let mut response_body = vec![0_u8; length];
        stream.read_exact(&mut response_body).await?;

        self.decode_response(&response_body)
    }

    fn encode_request(&self, request: &RequestEnvelope) -> anyhow::Result<Vec<u8>> {
        match self.format {
            BridgeFormat::Msgpack => Ok(encode_msgpack_request(request)?),
            BridgeFormat::Json => Ok(encode_json_request(request)?.into_bytes()),
        }
    }

    fn decode_response(&self, bytes: &[u8]) -> anyhow::Result<ResponseEnvelope> {
        match self.format {
            BridgeFormat::Msgpack => Ok(decode_msgpack_response(bytes)?),
            BridgeFormat::Json => {
                let text = std::str::from_utf8(bytes).context("bridge JSON response was not UTF-8")?;
                Ok(decode_json_response(text)?)
            }
        }
    }
}
