<script setup lang="ts">
import { onMounted, onUnmounted } from "vue";
import { useTheme } from "@/composables/useTheme";
import { useSettingsStore } from "@/stores/settings";
import { useHostdStore } from "@/stores/hostd";

import AppHeader from "@/components/AppHeader.vue";
import ConnectionSettings from "@/components/ConnectionSettings.vue";
import AudioSourcePanel from "@/components/AudioSourcePanel.vue";
import AudioDevicePanel from "@/components/AudioDevicePanel.vue";
import RuntimePanel from "@/components/RuntimePanel.vue";
import LogViewer from "@/components/LogViewer.vue";
import StatusBar from "@/components/StatusBar.vue";

useTheme();

const settings = useSettingsStore();
const hostd = useHostdStore();

onMounted(async () => {
  await hostd.setupListeners();
  await settings.initialize();
});

onUnmounted(() => {
  hostd.cleanup();
});
</script>

<template>
  <AppHeader />

  <div class="main-row">
    <ConnectionSettings />
    <AudioSourcePanel />
  </div>

  <AudioDevicePanel />
  <RuntimePanel />
  <LogViewer />
  <StatusBar />
</template>

<style scoped>
.main-row {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: var(--space-lg);
}

@media (max-width: 700px) {
  .main-row {
    grid-template-columns: 1fr;
  }
}
</style>
