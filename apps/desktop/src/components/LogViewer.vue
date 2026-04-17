<script setup lang="ts">
import { useHostdStore } from "@/stores/hostd";
import { storeToRefs } from "pinia";
import { ref, watch, nextTick } from "vue";

const hostd = useHostdStore();
const { logs } = storeToRefs(hostd);
const scrollContainer = ref<HTMLDivElement | null>(null);

watch(
  () => logs.value.length,
  async () => {
    await nextTick();
    if (scrollContainer.value) {
      scrollContainer.value.scrollTop = scrollContainer.value.scrollHeight;
    }
  }
);
</script>

<template>
  <div class="log-viewer card-stagger">
    <div class="log-viewer__header">
      <span class="log-viewer__title">Logs</span>
      <button
        v-if="logs.length > 0"
        class="log-viewer__clear"
        @click="hostd.clearLogs()"
      >
        Clear
      </button>
    </div>
    <div ref="scrollContainer" class="log-viewer__content">
      <div
        v-for="(entry, index) in logs"
        :key="index"
        :class="[
          'log-viewer__entry',
          'log-entry-animate',
          { 'log-viewer__entry--error': entry.is_error },
        ]"
      >
        <span class="log-viewer__timestamp">[{{ entry.timestamp }}]</span>
        <span class="log-viewer__prefix">{{
          entry.is_error ? "[stderr]" : "[stdout]"
        }}</span>
        <span class="log-viewer__message">{{ entry.message }}</span>
      </div>
      <div v-if="logs.length === 0" class="log-viewer__empty">
        No log output yet.
      </div>
    </div>
  </div>
</template>

<style scoped>
.log-viewer {
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
  overflow: hidden;
}

.log-viewer__header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: var(--space-lg) var(--space-lg) var(--space-sm);
}

.log-viewer__title {
  font-weight: 600;
  font-size: 14px;
}

.log-viewer__clear {
  font-size: 12px;
  padding: 2px 8px;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  transition: all var(--transition-fast);
}

.log-viewer__clear:hover {
  background: var(--bg-hover);
}

.log-viewer__content {
  flex: 1;
  overflow-y: auto;
  overflow-x: auto;
  padding: 0 var(--space-lg) var(--space-lg);
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.6;
}

.log-viewer__entry {
  white-space: pre;
}

.log-viewer__entry--error {
  color: var(--color-error-text);
}

.log-viewer__timestamp {
  color: var(--text-muted);
}

.log-viewer__prefix {
  color: var(--text-muted);
  margin: 0 4px;
}

.log-viewer__message {
  color: var(--text-primary);
}

.log-viewer__entry--error .log-viewer__message {
  color: var(--color-error-text);
}

.log-viewer__empty {
  color: var(--text-muted);
  font-family: var(--font-sans);
  font-style: italic;
  padding: var(--space-lg) 0;
}
</style>
