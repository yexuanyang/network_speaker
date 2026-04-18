use crate::models::AudioDeviceInfo;

pub async fn enumerate_devices(hostd_path: &str) -> Vec<AudioDeviceInfo> {
    let result = tokio::time::timeout(std::time::Duration::from_secs(5), async {
        let mut cmd = tokio::process::Command::new(hostd_path);
        cmd.arg("--list-devices");

        #[cfg(windows)]
        {
            const CREATE_NO_WINDOW: u32 = 0x08000000;
            cmd.creation_flags(CREATE_NO_WINDOW);
        }

        let output = cmd.output().await?;

        if !output.status.success() {
            return Ok::<Vec<AudioDeviceInfo>, Box<dyn std::error::Error + Send + Sync>>(Vec::new());
        }

        let stdout = String::from_utf8_lossy(&output.stdout);
        let devices: Vec<AudioDeviceInfo> = serde_json::from_str(&stdout)?;
        Ok(devices)
    })
    .await;

    match result {
        Ok(Ok(devices)) => devices,
        _ => Vec::new(),
    }
}
