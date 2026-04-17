use std::fmt;
use serde::Serialize;

#[derive(Debug, Serialize)]
pub enum AppError {
    Validation(String),
    Process(String),
    Io(String),
    Settings(String),
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::Validation(msg) => write!(f, "Validation error: {msg}"),
            AppError::Process(msg) => write!(f, "Process error: {msg}"),
            AppError::Io(msg) => write!(f, "IO error: {msg}"),
            AppError::Settings(msg) => write!(f, "Settings error: {msg}"),
        }
    }
}

impl std::error::Error for AppError {}

impl From<std::io::Error> for AppError {
    fn from(err: std::io::Error) -> Self {
        AppError::Io(err.to_string())
    }
}

impl From<serde_json::Error> for AppError {
    fn from(err: serde_json::Error) -> Self {
        AppError::Settings(err.to_string())
    }
}
