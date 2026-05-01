use crate::envelope::PROTOCOL_VERSION;
use crate::{ProtocolError, ProtocolResult, RequestEnvelope, ResponseEnvelope};

pub fn encode_msgpack_request(value: &RequestEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_request(bytes: &[u8]) -> ProtocolResult<RequestEnvelope> {
    let request =
        rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))?;
    validate_request(request)
}

pub fn encode_msgpack_response(value: &ResponseEnvelope) -> ProtocolResult<Vec<u8>> {
    rmp_serde::to_vec_named(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_msgpack_response(bytes: &[u8]) -> ProtocolResult<ResponseEnvelope> {
    let response =
        rmp_serde::from_slice(bytes).map_err(|error| ProtocolError::Decode(error.to_string()))?;
    validate_response(response)
}

pub fn encode_json_request(value: &RequestEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_request(text: &str) -> ProtocolResult<RequestEnvelope> {
    let request =
        serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))?;
    validate_request(request)
}

pub fn encode_json_response(value: &ResponseEnvelope) -> ProtocolResult<String> {
    serde_json::to_string(value).map_err(|error| ProtocolError::Encode(error.to_string()))
}

pub fn decode_json_response(text: &str) -> ProtocolResult<ResponseEnvelope> {
    let response =
        serde_json::from_str(text).map_err(|error| ProtocolError::Decode(error.to_string()))?;
    validate_response(response)
}

fn validate_request(request: RequestEnvelope) -> ProtocolResult<RequestEnvelope> {
    if request.protocol_version == PROTOCOL_VERSION {
        Ok(request)
    } else {
        Err(ProtocolError::UnsupportedProtocolVersion {
            actual: request.protocol_version,
            expected: PROTOCOL_VERSION,
        })
    }
}

fn validate_response(response: ResponseEnvelope) -> ProtocolResult<ResponseEnvelope> {
    if response.protocol_version == PROTOCOL_VERSION {
        Ok(response)
    } else {
        Err(ProtocolError::UnsupportedProtocolVersion {
            actual: response.protocol_version,
            expected: PROTOCOL_VERSION,
        })
    }
}
