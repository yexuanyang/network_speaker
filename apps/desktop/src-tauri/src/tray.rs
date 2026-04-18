use std::sync::Arc;

use tauri::{
    image::Image,
    menu::{MenuBuilder, MenuItem, MenuItemBuilder},
    tray::TrayIconBuilder,
    AppHandle, Emitter, Manager,
};

use crate::hostd_process::HostdProcessManager;

/// Holds a reference to the toggle menu item so we can update its label at runtime.
pub struct TrayMenuState {
    toggle_item: MenuItem<tauri::Wry>,
}

pub fn setup_tray(app: &AppHandle) -> tauri::Result<()> {
    let show_item = MenuItemBuilder::with_id("show", "Show Window").build(app)?;
    let toggle_item =
        MenuItemBuilder::with_id("toggle_streaming", "Start Streaming").build(app)?;
    let quit_item = MenuItemBuilder::with_id("quit", "Exit").build(app)?;

    let menu = MenuBuilder::new(app)
        .item(&show_item)
        .item(&toggle_item)
        .separator()
        .item(&quit_item)
        .build()?;

    app.manage(TrayMenuState {
        toggle_item: toggle_item.clone(),
    });

    let icon = Image::from_bytes(include_bytes!("../icons/32x32.png"))?;

    let app_handle = app.clone();
    TrayIconBuilder::with_id("main")
        .icon(icon)
        .menu(&menu)
        .tooltip("Network Speaker")
        .on_menu_event(move |tray_app, event| {
            let id = event.id().as_ref();
            match id {
                "show" => {
                    if let Some(window) = tray_app.get_webview_window("main") {
                        let _ = window.show();
                        let _ = window.unminimize();
                        let _ = window.set_focus();
                    }
                }
                "toggle_streaming" => {
                    let handle = tray_app.clone();
                    tauri::async_runtime::spawn(async move {
                        let mgr = handle.state::<Arc<HostdProcessManager>>();
                        if mgr.is_running().await {
                            let _ = mgr.stop().await;
                        } else {
                            // Start requires config from frontend; emit event to trigger it
                            let _ = handle.emit("tray-start-requested", ());
                        }
                    });
                }
                "quit" => {
                    let handle = tray_app.clone();
                    tauri::async_runtime::spawn(async move {
                        let mgr = handle.state::<Arc<HostdProcessManager>>();
                        if mgr.is_running().await {
                            let _ = mgr.stop().await;
                        }
                        handle.exit(0);
                    });
                }
                _ => {}
            }
        })
        .on_tray_icon_event(move |tray, event| {
            if let tauri::tray::TrayIconEvent::DoubleClick { .. } = event {
                if let Some(window) = tray.app_handle().get_webview_window("main") {
                    let _ = window.show();
                    let _ = window.unminimize();
                    let _ = window.set_focus();
                }
            }
        })
        .build(&app_handle)?;

    Ok(())
}

pub fn update_tray_label(app: &AppHandle, is_running: bool) {
    if let Some(state) = app.try_state::<TrayMenuState>() {
        let text = if is_running {
            "Stop Streaming"
        } else {
            "Start Streaming"
        };
        let _ = state.toggle_item.set_text(text);
    }
}
