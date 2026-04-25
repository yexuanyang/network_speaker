use crate::models::{CaptureSource, HostdCommand, LaunchConfiguration, WasapiRole};

pub fn build_command(exe_path: &str, config: &LaunchConfiguration) -> Result<HostdCommand, String> {
    crate::models::validate_config(config)?;

    let mut args = vec![
        "--host".into(),
        config.host.trim().to_string(),
        "--port".into(),
        config.port.to_string(),
    ];

    let source_str = match config.source {
        CaptureSource::Wasapi => "wasapi",
        CaptureSource::Pulse => "pulse",
        CaptureSource::Sine => "sine",
    };
    args.push("--source".into());
    args.push(source_str.into());

    if config.source == CaptureSource::Wasapi {
        let role_str = match config.wasapi_role {
            WasapiRole::Auto => "auto",
            WasapiRole::Multimedia => "multimedia",
            WasapiRole::Console => "console",
            WasapiRole::Communications => "communications",
        };
        args.push("--wasapi-role".into());
        args.push(role_str.into());

        if let Some(ref device_id) = config.device_id {
            if !device_id.is_empty() {
                args.push("--device".into());
                args.push(device_id.clone());
            }
        }
    }

    if config.source == CaptureSource::Pulse {
        if let Some(ref pulse_source) = config.pulse_source {
            if !pulse_source.is_empty() {
                args.push("--pulse-source".into());
                args.push(pulse_source.clone());
            }
        }
    }

    if let Some(seconds) = config.seconds {
        args.push("--seconds".into());
        args.push(seconds.to_string());
    }

    let display = format!(
        "\"{}\" {}",
        exe_path,
        args.iter()
            .map(|a| if a.contains(' ') {
                format!("\"{a}\"")
            } else {
                a.clone()
            })
            .collect::<Vec<_>>()
            .join(" ")
    );

    Ok(HostdCommand {
        executable_path: exe_path.to_string(),
        arguments: args,
        display_command_line: display,
    })
}

pub fn build_preview(exe_path: Option<&str>, config: &LaunchConfiguration) -> String {
    let path = exe_path.unwrap_or("hostd");
    match build_command(path, config) {
        Ok(cmd) => cmd.display_command_line,
        Err(msg) => msg,
    }
}
