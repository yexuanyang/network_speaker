<script setup lang="ts">
defineProps<{
  label?: string;
  modelValue: string;
  options: { value: string; label: string }[];
  disabled?: boolean;
}>();

defineEmits<{
  "update:modelValue": [value: string];
}>();
</script>

<template>
  <div class="base-select">
    <label v-if="label" class="base-select__label">{{ label }}</label>
    <select
      :value="modelValue"
      :disabled="disabled"
      class="base-select__field"
      @change="$emit('update:modelValue', ($event.target as HTMLSelectElement).value)"
    >
      <option
        v-for="option in options"
        :key="option.value"
        :value="option.value"
      >
        {{ option.label }}
      </option>
    </select>
  </div>
</template>

<style scoped>
.base-select {
  display: flex;
  flex-direction: column;
  gap: var(--space-xs);
}

.base-select__label {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
}

.base-select__field {
  padding: 7px 10px;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--text-primary);
  outline: none;
  cursor: pointer;
  transition: border-color var(--transition-fast), box-shadow var(--transition-fast);
}

.base-select__field:focus {
  border-color: var(--border-focus);
  box-shadow: var(--shadow-focus);
}

.base-select__field:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
</style>
