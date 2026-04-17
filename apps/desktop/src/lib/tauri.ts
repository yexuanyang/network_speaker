import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type {
  AudioDeviceInfo,
  HostdStatus,
  LaunchConfiguration,
  LogEntry,
  PlatformInfo,
} from "@/types";

export function getPlatformInfo(): Promise<PlatformInfo> {
  return invoke("get_platform_info");
}

export function loadSettings(): Promise<LaunchConfiguration> {
  return invoke("load_settings");
}

export function saveSettings(config: LaunchConfiguration): Promise<void> {
  return invoke("save_settings", { config });
}

export function getHostdPath(): Promise<string | null> {
  return invoke("get_hostd_path");
}

export function getCommandPreview(config: LaunchConfiguration): Promise<string> {
  return invoke("get_command_preview", { config });
}

export function enumerateDevices(): Promise<AudioDeviceInfo[]> {
  return invoke("enumerate_devices");
}

export function checkVirtualAudio(
  devices: AudioDeviceInfo[]
): Promise<boolean> {
  return invoke("check_virtual_audio", { devices });
}

export function startStreaming(config: LaunchConfiguration): Promise<void> {
  return invoke("start_streaming", { config });
}

export function stopStreaming(): Promise<void> {
  return invoke("stop_streaming");
}

export function getProcessState(): Promise<HostdStatus> {
  return invoke("get_process_state");
}

export function validateConfig(config: LaunchConfiguration): Promise<void> {
  return invoke("validate_config", { config });
}

export function onHostdLog(
  callback: (entry: LogEntry) => void
): Promise<UnlistenFn> {
  return listen<LogEntry>("hostd-log", (event) => callback(event.payload));
}

export function onHostdStateChanged(
  callback: (status: HostdStatus) => void
): Promise<UnlistenFn> {
  return listen<HostdStatus>("hostd-state-changed", (event) =>
    callback(event.payload)
  );
}

export function onTrayStartRequested(
  callback: () => void
): Promise<UnlistenFn> {
  return listen("tray-start-requested", () => callback());
}
