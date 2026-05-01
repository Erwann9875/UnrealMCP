use thiserror::Error;

pub type ProtocolResult<T> = Result<T, ProtocolError>;

#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("unsupported protocol version {actual}, expected {expected}")]
    UnsupportedProtocolVersion { actual: u16, expected: u16 },

    #[error("message serialization failed: {0}")]
    Encode(String),

    #[error("message deserialization failed: {0}")]
    Decode(String),
}
