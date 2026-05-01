use serde::{Deserialize, Serialize};

use crate::Command;
use crate::CommandResult;

pub const PROTOCOL_VERSION: u16 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ResponseMode {
    Summary,
    Handles,
    Full,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorMode {
    Stop,
    Continue,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RequestEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    pub response_mode: ResponseMode,
    pub error_mode: ErrorMode,
    pub commands: Vec<Command>,
}

impl RequestEnvelope {
    pub fn new(
        request_id: u64,
        response_mode: ResponseMode,
        error_mode: ErrorMode,
        commands: Vec<Command>,
    ) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            response_mode,
            error_mode,
            commands,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct IndexedError {
    pub command_index: usize,
    pub item_index: Option<usize>,
    pub code: String,
    pub message: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResponseEnvelope {
    pub protocol_version: u16,
    pub request_id: u64,
    pub ok: bool,
    pub elapsed_ms: u32,
    pub results: Vec<CommandResult>,
    pub errors: Vec<IndexedError>,
}

impl ResponseEnvelope {
    pub fn success(request_id: u64, elapsed_ms: u32, results: Vec<CommandResult>) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            ok: true,
            elapsed_ms,
            results,
            errors: Vec::new(),
        }
    }

    pub fn failure(request_id: u64, elapsed_ms: u32, errors: Vec<IndexedError>) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            request_id,
            ok: false,
            elapsed_ms,
            results: Vec::new(),
            errors,
        }
    }
}
