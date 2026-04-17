<script setup lang="ts">
defineProps<{
  variant?: "primary" | "secondary" | "danger";
  disabled?: boolean;
  loading?: boolean;
}>();

defineEmits<{
  click: [];
}>();
</script>

<template>
  <button
    :class="['base-button', `base-button--${variant ?? 'secondary'}`]"
    :disabled="disabled || loading"
    @click="$emit('click')"
  >
    <svg
      v-if="loading"
      class="spinner base-button__spinner"
      width="16"
      height="16"
      viewBox="0 0 16 16"
      fill="none"
    >
      <circle
        cx="8"
        cy="8"
        r="6"
        stroke="currentColor"
        stroke-width="2"
        stroke-linecap="round"
        stroke-dasharray="30"
        stroke-dashoffset="10"
      />
    </svg>
    <slot />
  </button>
</template>

<style scoped>
.base-button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: var(--space-sm);
  padding: 7px 16px;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  font-weight: 500;
  cursor: pointer;
  outline: none;
  transition: all var(--transition-fast);
  user-select: none;
}

.base-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  transform: none;
}

.base-button:not(:disabled):active {
  transform: scale(0.97);
}

/* Secondary (default) */
.base-button--secondary {
  background: var(--bg-card);
  color: var(--text-primary);
}

.base-button--secondary:not(:disabled):hover {
  background: var(--bg-hover);
}

/* Primary */
.base-button--primary {
  background: var(--color-primary);
  border-color: var(--color-primary);
  color: var(--text-inverse);
}

.base-button--primary:not(:disabled):hover {
  background: var(--color-primary-hover);
  border-color: var(--color-primary-hover);
}

/* Danger */
.base-button--danger {
  background: var(--color-danger);
  border-color: var(--color-danger);
  color: var(--text-inverse);
}

.base-button--danger:not(:disabled):hover {
  background: var(--color-danger-hover);
  border-color: var(--color-danger-hover);
}

.base-button__spinner {
  flex-shrink: 0;
}
</style>
