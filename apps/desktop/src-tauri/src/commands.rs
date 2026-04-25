use std::sync::Arc;

use tauri::{Manager, State};

use crate::device_enumerator;
use crate::hostd_command;
use crate::hostd_locator;
use crate::hostd_process::HostdProcessManager;
use crate::models::{AudioDeviceInfo, HostdStatus, LaunchConfiguration, PlatformInfo};
use crate::platform;
use crate::settings;
use crate::virtual_audio;

#[tauri::command]
pub fn get_platform_info() -> PlatformInfo {
    PlatformInfo {
        os: platform::current_os().into(),
        available_sources: platform::available_sources(),
    }
}

#[tauri::command]
pub fn load_settings() -> LaunchConfiguration {
    settings::load()
}

#[tauri::command]
pub fn save_settings(config: LaunchConfiguration) -> Result<(), String> {
    settings::save(&config).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn get_hostd_path(app_handle: tauri::AppHandle) -> Option<String> {
    let base_dir = app_handle.path().resource_dir().unwrap_or_else(|_| {
        std::env::current_exe()
            .unwrap_or_default()
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .to_path_buf()
    });

    hostd_locator::locate(&base_dir).map(|p| p.to_string_lossy().into_owned())
}

#[tauri::command]
pub fn get_command_preview(config: LaunchConfiguration) -> String {
    let hostd_path = "hostd";
    hostd_command::build_preview(Some(hostd_path), &config)
}

#[tauri::command]
pub async fn enumerate_devices(app_handle: tauri::AppHandle) -> Vec<AudioDeviceInfo> {
    let base_dir = app_handle.path().resource_dir().unwrap_or_else(|_| {
        std::env::current_exe()
            .unwrap_or_default()
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .to_path_buf()
    });

    let hostd_path = match hostd_locator::locate(&base_dir) {
        Some(p) => p.to_string_lossy().into_owned(),
        None => return Vec::new(),
    };

    device_enumerator::enumerate_devices(&hostd_path).await
}

#[tauri::command]
pub fn check_virtual_audio(devices: Vec<AudioDeviceInfo>) -> bool {
    virtual_audio::contains_virtual_device(&devices)
}

#[tauri::command]
pub async fn start_streaming(
    config: LaunchConfiguration,
    process_manager: State<'_, Arc<HostdProcessManager>>,
    app_handle: tauri::AppHandle,
) -> Result<(), String> {
    crate::models::validate_config(&config)?;

    settings::save(&config).map_err(|e| e.to_string())?;

    let base_dir = app_handle.path().resource_dir().unwrap_or_else(|_| {
        std::env::current_exe()
            .unwrap_or_default()
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."))
            .to_path_buf()
    });

    let hostd_path = hostd_locator::locate(&base_dir).ok_or_else(|| {
        "hostd binary not found. Build or install the bundled launcher.".to_string()
    })?;

    let hostd_path_str = hostd_path.to_string_lossy().into_owned();
    let command = hostd_command::build_command(&hostd_path_str, &config)?;

    process_manager.start(command).await
}

#[tauri::command]
pub async fn stop_streaming(
    process_manager: State<'_, Arc<HostdProcessManager>>,
) -> Result<(), String> {
    process_manager.stop().await
}

#[tauri::command]
pub async fn get_process_state(
    process_manager: State<'_, Arc<HostdProcessManager>>,
) -> Result<HostdStatus, String> {
    let state = process_manager.state().await;
    let inner_status = process_manager.status(None, String::new()).await;
    Ok(HostdStatus {
        state,
        status_text: inner_status.status_text,
        hostd_path: inner_status.hostd_path,
        command_preview: inner_status.command_preview,
    })
}

#[tauri::command]
pub fn validate_config(config: LaunchConfiguration) -> Result<(), String> {
    crate::models::validate_config(&config)
}
