use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CaptureSource {
    Wasapi,
    Pulse,
    Sine,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum WasapiRole {
    Auto,
    Multimedia,
    Console,
    Communications,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ProcessState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Faulted,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AudioDeviceInfo {
    pub id: String,
    pub name: String,
    pub description: String,
    #[serde(alias = "default")]
    pub is_default: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LaunchConfiguration {
    pub host: String,
    pub port: u16,
    pub source: CaptureSource,
    pub wasapi_role: WasapiRole,
    pub pulse_source: Option<String>,
    pub seconds: Option<u32>,
    pub device_id: Option<String>,
}

impl Default for LaunchConfiguration {
    fn default() -> Self {
        Self {
            host: String::new(),
            port: 50000,
            source: crate::platform::default_source(),
            wasapi_role: WasapiRole::Multimedia,
            pulse_source: None,
            seconds: None,
            device_id: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogEntry {
    pub timestamp: String,
    pub message: String,
    pub is_error: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HostdStatus {
    pub state: ProcessState,
    pub status_text: String,
    pub hostd_path: Option<String>,
    pub command_preview: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlatformInfo {
    pub os: String,
    pub available_sources: Vec<CaptureSource>,
}

pub struct HostdCommand {
    pub executable_path: String,
    pub arguments: Vec<String>,
    pub display_command_line: String,
}

pub fn validate_config(config: &LaunchConfiguration) -> Result<(), String> {
    if config.host.trim().is_empty() {
        return Err("Target IP is required.".into());
    }

    if config.host.trim().parse::<std::net::IpAddr>().is_err() {
        return Err("Target IP must be a valid IP address.".into());
    }

    if config.port == 0 {
        return Err("Port must be between 1 and 65535.".into());
    }

    if let Some(seconds) = config.seconds {
        if seconds == 0 {
            return Err("Seconds must be a positive integer when set.".into());
        }
    }

    Ok(())
}
