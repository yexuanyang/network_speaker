import { defineStore } from "pinia";
import { ref, computed, watch } from "vue";
import type {
  CaptureSource,
  WasapiRole,
  AudioDeviceInfo,
  PlatformInfo,
} from "@/types";
import {
  getPlatformInfo,
  loadSettings,
  saveSettings,
  getHostdPath,
  getCommandPreview,
  enumerateDevices,
  checkVirtualAudio,
} from "@/lib/tauri";

const DEFAULT_SENTINEL: AudioDeviceInfo = {
  id: "",
  name: "(Default Device)",
  description: "",
  is_default: false,
};

export const useSettingsStore = defineStore("settings", () => {
  // Form state
  const host = ref("");
  const port = ref("50000");
  const seconds = ref("");
  const source = ref<CaptureSource>("sine");
  const wasapiRole = ref<WasapiRole>("multimedia");
  const pulseSource = ref("");
  const deviceId = ref<string | null>(null);

  // Device state
  const devices = ref<AudioDeviceInfo[]>([DEFAULT_SENTINEL]);
  const selectedDevice = ref<AudioDeviceInfo>(DEFAULT_SENTINEL);
  const hasVirtualAudio = ref(false);
  const isDeviceListLoading = ref(false);

  // Runtime
  const hostdPath = ref<string | null>(null);
  const commandPreview = ref("hostd not found");
  const platform = ref<PlatformInfo | null>(null);

  // Computed
  const availableSources = computed(() => {
    if (!platform.value) return [];
    return platform.value.available_sources.map((s) => ({
      value: s,
      label: s.charAt(0).toUpperCase() + s.slice(1),
    }));
  });

  const isDeviceSelectionEnabled = computed(() => {
    return (
      (source.value === "wasapi" || source.value === "pulse") &&
      !isDeviceListLoading.value
    );
  });

  const isWasapiRoleEnabled = computed(() => {
    return platform.value?.os === "windows" && source.value === "wasapi";
  });

  const isPulseSourceEnabled = computed(() => {
    return platform.value?.os === "linux" && source.value === "pulse";
  });

  function buildConfig() {
    return {
      host: host.value.trim(),
      port: parseInt(port.value) || 50000,
      source: source.value,
      wasapi_role: wasapiRole.value,
      pulse_source: pulseSource.value || null,
      seconds: seconds.value ? parseInt(seconds.value) || null : null,
      device_id:
        selectedDevice.value && selectedDevice.value.id
          ? selectedDevice.value.id
          : null,
    };
  }

  async function updateCommandPreview() {
    try {
      const config = buildConfig();
      commandPreview.value = await getCommandPreview(config);
    } catch {
      commandPreview.value = "Configuration is invalid.";
    }
  }

  // Watch for config changes to update preview
  watch([host, port, seconds, source, wasapiRole, pulseSource, selectedDevice], () => {
    updateCommandPreview();
  });

  async function initialize() {
    platform.value = await getPlatformInfo();
    hostdPath.value = await getHostdPath();

    const settings = await loadSettings();
    host.value = settings.host;
    port.value = settings.port.toString();
    source.value = settings.source;
    wasapiRole.value = settings.wasapi_role;
    pulseSource.value = settings.pulse_source ?? "";
    seconds.value = settings.seconds?.toString() ?? "";
    deviceId.value = settings.device_id;

    if (!hostdPath.value) {
      commandPreview.value =
        "hostd not found. Build or install the bundled launcher.";
    }

    await refreshDevices();
    await updateCommandPreview();
  }

  async function refreshDevices() {
    if (!hostdPath.value) return;

    isDeviceListLoading.value = true;
    try {
      const deviceList = await enumerateDevices();

      const previousDeviceId = selectedDevice.value?.id || deviceId.value;

      // Build sentinel with default device name
      const defaultDevice = deviceList.find((d) => d.is_default);
      const sentinelName = defaultDevice
        ? `(Default Device \u2014 ${defaultDevice.name})`
        : "(Default Device)";
      const sentinel: AudioDeviceInfo = { ...DEFAULT_SENTINEL, name: sentinelName };

      devices.value = [sentinel, ...deviceList];
      hasVirtualAudio.value = await checkVirtualAudio(deviceList);

      // Restore previous selection
      if (previousDeviceId) {
        const match = devices.value.find((d) => d.id === previousDeviceId);
        if (match) {
          selectedDevice.value = match;
        } else {
          selectedDevice.value = sentinel;
        }
      } else {
        selectedDevice.value = sentinel;
      }
    } finally {
      isDeviceListLoading.value = false;
    }
  }

  async function saveCurrentSettings() {
    const config = buildConfig();
    await saveSettings(config);
  }

  return {
    host,
    port,
    seconds,
    source,
    wasapiRole,
    pulseSource,
    deviceId,
    devices,
    selectedDevice,
    hasVirtualAudio,
    isDeviceListLoading,
    hostdPath,
    commandPreview,
    platform,
    availableSources,
    isDeviceSelectionEnabled,
    isWasapiRoleEnabled,
    isPulseSourceEnabled,
    buildConfig,
    initialize,
    refreshDevices,
    saveCurrentSettings,
    updateCommandPreview,
  };
});
