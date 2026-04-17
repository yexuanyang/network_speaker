<script setup lang="ts">
import BaseInput from "./ui/BaseInput.vue";
import BaseButton from "./ui/BaseButton.vue";
import { useSettingsStore } from "@/stores/settings";
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";

const settings = useSettingsStore();
const hostd = useHostdStore();
const { hostdPath, commandPreview } = storeToRefs(settings);
const { canStart, canStop } = storeToRefs(hostd);

async function onStart() {
  await hostd.start();
}

async function onStop() {
  await hostd.stop();
}
</script>

<template>
  <div class="runtime card-stagger">
    <span class="runtime__title">Runtime</span>

    <div class="runtime__fields">
      <BaseInput
        :model-value="hostdPath ?? 'hostd not found'"
        label="hostd Path"
        readonly
      />
      <BaseInput
        :model-value="commandPreview"
        label="Command Preview"
        readonly
      />
    </div>

    <div class="runtime__actions">
      <BaseButton variant="primary" :disabled="!canStart" @click="onStart">
        Start
      </BaseButton>
      <BaseButton variant="danger" :disabled="!canStop" @click="onStop">
        Stop
      </BaseButton>
    </div>
  </div>
</template>

<style scoped>
.runtime {
  padding: var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
}

.runtime__title {
  font-weight: 600;
  font-size: 14px;
}

.runtime__fields {
  display: flex;
  flex-direction: column;
  gap: var(--space-md);
  margin-top: var(--space-md);
}

.runtime__actions {
  display: flex;
  gap: var(--space-md);
  margin-top: var(--space-lg);
}

.runtime__actions .base-button {
  min-width: 120px;
}
</style>
