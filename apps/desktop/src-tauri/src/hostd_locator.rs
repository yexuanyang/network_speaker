use std::path::{Path, PathBuf};

use crate::platform::hostd_binary_name;

pub fn locate(base_dir: &Path) -> Option<PathBuf> {
    // 1. Environment variable override
    if let Ok(env_path) = std::env::var("NSPEAKER_HOSTD_PATH") {
        let p = PathBuf::from(&env_path);
        if p.is_file() {
            return Some(p);
        }
    }

    let binary = hostd_binary_name();

    // 2. Same directory as the running binary (bundled sidecar in production builds)
    let same_dir = base_dir.join(binary);
    if same_dir.is_file() {
        return Some(same_dir);
    }

    // 3. Tauri externalBin sidecar in binaries/ directory (dev mode).
    //    Tauri names sidecars as binaries/hostd-{target_triple}[.exe] at build time.
    //    In production builds these are extracted flat alongside the app,
    //    but in dev mode they stay in the binaries/ subdirectory.
    let binaries_dir = base_dir.join("binaries");
    if binaries_dir.is_dir() {
        if let Ok(entries) = std::fs::read_dir(&binaries_dir) {
            for entry in entries.flatten() {
                let name = entry.file_name();
                let name_str = name.to_string_lossy();
                if name_str.starts_with("hostd-") && entry.path().is_file() {
                    return Some(entry.path());
                }
            }
        }
    }

    // 4. Directory of the current executable (e.g. both installed to /usr/bin/)
    if let Ok(exe) = std::env::current_exe() {
        if let Some(exe_dir) = exe.parent() {
            let candidate = exe_dir.join(binary);
            if candidate.is_file() {
                return Some(candidate);
            }
        }
    }

    // 5. Search in system PATH (e.g. hostd installed via system package manager)
    if let Some(p) = search_in_path(binary) {
        return Some(p);
    }

    None
}

/// Search for an executable in the system PATH.
/// Uses `;` as separator on Windows and `:` on Unix, matching each platform's convention.
fn search_in_path(binary: &str) -> Option<PathBuf> {
    let path_var = std::env::var("PATH").ok()?;
    let separator = if cfg!(windows) { ';' } else { ':' };
    for dir in path_var.split(separator) {
        let candidate = PathBuf::from(dir).join(binary);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    None
}
