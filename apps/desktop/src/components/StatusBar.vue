<script setup lang="ts">
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";
import { computed } from "vue";

const hostd = useHostdStore();
const { state, statusText } = storeToRefs(hostd);

const stateClass = computed(() => {
  switch (state.value) {
    case "running":
    case "starting":
      return "status-bar--active";
    case "faulted":
      return "status-bar--error";
    case "stopping":
      return "status-bar--warning";
    default:
      return "";
  }
});
</script>

<template>
  <div :class="['status-bar', 'card-stagger', stateClass]">
    <span class="status-bar__text">{{ statusText }}</span>
  </div>
</template>

<style scoped>
.status-bar {
  padding: var(--space-md) var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-status-bar);
  transition: background-color var(--transition-normal),
    border-color var(--transition-normal),
    color var(--transition-normal);
}

.status-bar--active {
  background: color-mix(in srgb, var(--color-primary) 10%, transparent);
  border-color: color-mix(in srgb, var(--color-primary) 30%, transparent);
}

.status-bar--error {
  background: color-mix(in srgb, var(--color-error) 10%, transparent);
  border-color: color-mix(in srgb, var(--color-error) 30%, transparent);
}

.status-bar--warning {
  background: color-mix(in srgb, var(--color-warning) 10%, transparent);
  border-color: color-mix(in srgb, var(--color-warning) 30%, transparent);
}

.status-bar__text {
  font-size: 13px;
  color: var(--text-primary);
}

.status-bar--active .status-bar__text {
  color: var(--color-primary);
}

.status-bar--error .status-bar__text {
  color: var(--color-error-text);
}
</style>
