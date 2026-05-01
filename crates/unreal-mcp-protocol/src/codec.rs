use crate::{ProtocolError, ProtocolResult, RequestEnvelope, ResponseEnvelope};

pub fn encode_msgpack_request(value: &RequestEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_request(bytes: &[u8]) -> ProtocolResult<RequestEnvelope> {
    rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_msgpack_response(value: &ResponseEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_response(bytes: &[u8]) -> ProtocolResult<ResponseEnvelope> {
    rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_json_request(value: &RequestEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_request(text: &str) -> ProtocolResult<RequestEnvelope> {
    serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))
}

pub fn encode_json_response(value: &ResponseEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_response(text: &str) -> ProtocolResult<ResponseEnvelope> {
    serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))
}
