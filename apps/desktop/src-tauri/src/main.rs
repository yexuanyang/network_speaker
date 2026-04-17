#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::sync::Arc;

use tauri::Manager;

use network_speaker_desktop::commands;
use network_speaker_desktop::hostd_process::HostdProcessManager;

fn main() {
    // Work around WebKitGTK DMABUF rendering issues on Wayland (Fedora 43+, WebKitGTK 2.52+)
    #[cfg(target_os = "linux")]
    {
        if std::env::var("WEBKIT_DISABLE_DMABUF_RENDERER").is_err() {
            std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
        }
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_os::init())
        .setup(|app| {
            let handle = app.handle().clone();
            let process_manager = Arc::new(HostdProcessManager::new(handle.clone()));
            app.manage(process_manager);

            // Open devtools in debug builds
            #[cfg(debug_assertions)]
            if let Some(window) = app.get_webview_window("main") {
                window.open_devtools();
            }

            #[cfg(feature = "tray")]
            network_speaker_desktop::tray::setup_tray(&handle)?;

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_platform_info,
            commands::load_settings,
            commands::save_settings,
            commands::get_hostd_path,
            commands::get_command_preview,
            commands::enumerate_devices,
            commands::check_virtual_audio,
            commands::start_streaming,
            commands::stop_streaming,
            commands::get_process_state,
            commands::validate_config,
        ])
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let handle = window.app_handle().clone();
                let mgr = handle.state::<Arc<HostdProcessManager>>();
                let is_running =
                    tauri::async_runtime::block_on(async { mgr.is_running().await });

                if is_running {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
