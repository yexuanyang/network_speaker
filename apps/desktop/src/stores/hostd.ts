import { defineStore } from "pinia";
import { ref, computed } from "vue";
import type { ProcessState, LogEntry } from "@/types";
import {
  startStreaming,
  stopStreaming,
  onHostdLog,
  onHostdStateChanged,
  onTrayStartRequested,
} from "@/lib/tauri";
import { useSettingsStore } from "./settings";
import type { UnlistenFn } from "@tauri-apps/api/event";

export const useHostdStore = defineStore("hostd", () => {
  const state = ref<ProcessState>("stopped");
  const statusText = ref("Ready");
  const logs = ref<LogEntry[]>([]);

  const isRunning = computed(() =>
    ["starting", "running", "stopping"].includes(state.value)
  );
  const canStart = computed(() => !isRunning.value);
  const canStop = computed(() => isRunning.value);

  const unlisteners: UnlistenFn[] = [];

  async function setupListeners() {
    unlisteners.push(
      await onHostdStateChanged((status) => {
        state.value = status.state;
        statusText.value = status.status_text;
      })
    );

    unlisteners.push(
      await onHostdLog((entry) => {
        logs.value.push(entry);
        // Keep log buffer reasonable
        if (logs.value.length > 5000) {
          logs.value = logs.value.slice(-4000);
        }
      })
    );

    unlisteners.push(
      await onTrayStartRequested(() => {
        start();
      })
    );
  }

  function cleanup() {
    for (const unlisten of unlisteners) {
      unlisten();
    }
    unlisteners.length = 0;
  }

  async function start() {
    const settingsStore = useSettingsStore();
    const config = settingsStore.buildConfig();

    try {
      await startStreaming(config);
    } catch (error) {
      statusText.value = String(error);
    }
  }

  async function stop() {
    try {
      await stopStreaming();
    } catch (error) {
      statusText.value = String(error);
    }
  }

  function clearLogs() {
    logs.value = [];
  }

  return {
    state,
    statusText,
    logs,
    isRunning,
    canStart,
    canStop,
    setupListeners,
    cleanup,
    start,
    stop,
    clearLogs,
  };
});
