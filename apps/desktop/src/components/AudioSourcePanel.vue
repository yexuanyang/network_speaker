<script setup lang="ts">
import BaseSelect from "./ui/BaseSelect.vue";
import BaseInput from "./ui/BaseInput.vue";
import { useSettingsStore } from "@/stores/settings";
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";

const settings = useSettingsStore();
const hostd = useHostdStore();
const { source, wasapiRole, pulseSource, availableSources, isWasapiRoleEnabled, isPulseSourceEnabled } =
  storeToRefs(settings);
const { isRunning } = storeToRefs(hostd);

const wasapiRoleOptions = [
  { value: "auto", label: "Auto" },
  { value: "multimedia", label: "Multimedia" },
  { value: "console", label: "Console" },
  { value: "communications", label: "Communications" },
];
</script>

<template>
  <div class="audio-source card-stagger">
    <BaseSelect
      v-model="source"
      label="Source"
      :options="availableSources"
      :disabled="isRunning"
    />
    <Transition name="fade">
      <BaseSelect
        v-if="isWasapiRoleEnabled"
        v-model="wasapiRole"
        label="WASAPI Role"
        :options="wasapiRoleOptions"
        :disabled="isRunning"
      />
    </Transition>
    <Transition name="fade">
      <BaseInput
        v-if="isPulseSourceEnabled"
        v-model="pulseSource"
        label="Pulse Source (optional)"
        placeholder="Auto-detect"
        :disabled="isRunning"
      />
    </Transition>
  </div>
</template>

<style scoped>
.audio-source {
  display: flex;
  flex-wrap: wrap;
  gap: var(--space-md);
  padding: var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
}

.audio-source > * {
  flex: 1 1 120px;
  min-width: 0;
}
</style>
