<script setup lang="ts">
import BaseButton from "./ui/BaseButton.vue";
import StatusDot from "./ui/StatusDot.vue";
import { useSettingsStore } from "@/stores/settings";
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";
import { computed } from "vue";

const settings = useSettingsStore();
const hostd = useHostdStore();
const { devices, selectedDevice, hasVirtualAudio, isDeviceListLoading, isDeviceSelectionEnabled } =
  storeToRefs(settings);
const { isRunning } = storeToRefs(hostd);

const selectedDeviceId = computed({
  get: () => selectedDevice.value?.id ?? "",
  set: (id: string) => {
    const device = devices.value.find((d) => d.id === id);
    if (device) {
      selectedDevice.value = device;
    }
  },
});

async function onRefresh() {
  await settings.refreshDevices();
}
</script>

<template>
  <div class="audio-device card-stagger">
    <div class="audio-device__header">
      <span class="audio-device__title">Audio Device</span>
    </div>

    <div class="audio-device__controls">
      <select
        :value="selectedDeviceId"
        :disabled="!isDeviceSelectionEnabled || isRunning"
        class="audio-device__select"
        @change="selectedDeviceId = ($event.target as HTMLSelectElement).value"
      >
        <option
          v-for="device in devices"
          :key="device.id"
          :value="device.id"
        >
          {{ device.name }}
        </option>
      </select>
      <BaseButton
        :disabled="!isDeviceSelectionEnabled || isRunning"
        :loading="isDeviceListLoading"
        @click="onRefresh"
      >
        Refresh
      </BaseButton>
    </div>

    <Transition name="expand">
      <div v-if="hasVirtualAudio" class="audio-device__indicator audio-device__indicator--ok">
        <StatusDot color="green" />
        <span>Virtual audio cable detected. App-specific audio routing is available.</span>
      </div>
    </Transition>
    <Transition name="expand">
      <div v-if="!hasVirtualAudio && !isDeviceListLoading" class="audio-device__indicator audio-device__indicator--warn">
        <StatusDot color="yellow" />
        <span>
          No virtual audio cable detected. Install
          <a href="https://vb-audio.com/Cable/" target="_blank">VB-CABLE (free)</a>
          for app-specific audio routing. After installing, click Refresh.
        </span>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.audio-device {
  padding: var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
}

.audio-device__header {
  margin-bottom: var(--space-md);
}

.audio-device__title {
  font-weight: 600;
  font-size: 14px;
}

.audio-device__controls {
  display: flex;
  gap: var(--space-sm);
}

.audio-device__select {
  flex: 1;
  padding: 7px 10px;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--text-primary);
  outline: none;
  cursor: pointer;
  transition: border-color var(--transition-fast);
}

.audio-device__select:focus {
  border-color: var(--border-focus);
  box-shadow: var(--shadow-focus);
}

.audio-device__select:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.audio-device__indicator {
  display: flex;
  align-items: center;
  gap: var(--space-sm);
  margin-top: 10px;
  font-size: 13px;
}

.audio-device__indicator--ok {
  color: var(--color-success-text);
}

.audio-device__indicator--warn {
  color: var(--color-warning-text);
}
</style>
