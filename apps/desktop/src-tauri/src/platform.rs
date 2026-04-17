use crate::models::CaptureSource;

#[cfg(target_os = "windows")]
pub fn current_os() -> &'static str {
    "windows"
}

#[cfg(target_os = "linux")]
pub fn current_os() -> &'static str {
    "linux"
}

#[cfg(target_os = "windows")]
pub fn available_sources() -> Vec<CaptureSource> {
    vec![CaptureSource::Wasapi, CaptureSource::Sine]
}

#[cfg(target_os = "linux")]
pub fn available_sources() -> Vec<CaptureSource> {
    vec![CaptureSource::Pulse, CaptureSource::Sine]
}

#[cfg(target_os = "windows")]
pub fn default_source() -> CaptureSource {
    CaptureSource::Wasapi
}

#[cfg(target_os = "linux")]
pub fn default_source() -> CaptureSource {
    CaptureSource::Pulse
}

#[cfg(target_os = "windows")]
pub fn hostd_binary_name() -> &'static str {
    "hostd.exe"
}

#[cfg(target_os = "linux")]
pub fn hostd_binary_name() -> &'static str {
    "hostd"
}
