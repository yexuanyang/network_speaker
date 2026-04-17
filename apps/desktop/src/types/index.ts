export type CaptureSource = "wasapi" | "pulse" | "sine";
export type WasapiRole = "auto" | "multimedia" | "console" | "communications";
export type ProcessState =
  | "stopped"
  | "starting"
  | "running"
  | "stopping"
  | "faulted";

export interface AudioDeviceInfo {
  id: string;
  name: string;
  description: string;
  is_default: boolean;
}

export interface LaunchConfiguration {
  host: string;
  port: number;
  source: CaptureSource;
  wasapi_role: WasapiRole;
  pulse_source: string | null;
  seconds: number | null;
  device_id: string | null;
}

export interface PlatformInfo {
  os: "windows" | "linux";
  available_sources: CaptureSource[];
}

export interface HostdStatus {
  state: ProcessState;
  status_text: string;
  hostd_path: string | null;
  command_preview: string;
}

export interface LogEntry {
  timestamp: string;
  message: string;
  is_error: boolean;
}
