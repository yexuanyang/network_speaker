use std::path::PathBuf;

use crate::models::LaunchConfiguration;

fn settings_path() -> PathBuf {
    let config_dir = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
    config_dir.join("NetworkSpeaker").join("settings.json")
}

pub fn load() -> LaunchConfiguration {
    let path = settings_path();
    match std::fs::read_to_string(&path) {
        Ok(contents) => serde_json::from_str(&contents).unwrap_or_default(),
        Err(_) => LaunchConfiguration::default(),
    }
}

pub fn save(config: &LaunchConfiguration) -> Result<(), crate::error::AppError> {
    let path = settings_path();
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let json = serde_json::to_string_pretty(config)?;
    std::fs::write(&path, json)?;
    Ok(())
}
