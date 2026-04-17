<script setup lang="ts">
import BaseInput from "./ui/BaseInput.vue";
import { useSettingsStore } from "@/stores/settings";
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";

const settings = useSettingsStore();
const hostd = useHostdStore();
const { host, port, seconds } = storeToRefs(settings);
const { isRunning } = storeToRefs(hostd);
</script>

<template>
  <div class="connection-settings card-stagger">
    <BaseInput
      v-model="host"
      label="Target IP"
      placeholder="192.168.1.100"
      :disabled="isRunning"
    />
    <BaseInput
      v-model="port"
      label="UDP Port"
      placeholder="50000"
      :disabled="isRunning"
    />
    <BaseInput
      v-model="seconds"
      label="Seconds (optional)"
      placeholder="Unlimited"
      :disabled="isRunning"
    />
  </div>
</template>

<style scoped>
.connection-settings {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: var(--space-md);
  padding: var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
}
</style>
