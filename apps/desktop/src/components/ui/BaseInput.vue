<script setup lang="ts">
defineProps<{
  label?: string;
  modelValue: string;
  readonly?: boolean;
  disabled?: boolean;
  placeholder?: string;
  error?: string;
}>();

defineEmits<{
  "update:modelValue": [value: string];
}>();
</script>

<template>
  <div class="base-input">
    <label v-if="label" class="base-input__label">{{ label }}</label>
    <input
      :value="modelValue"
      :readonly="readonly"
      :disabled="disabled"
      :placeholder="placeholder"
      :class="['base-input__field', { 'base-input__field--error': error, 'base-input__field--readonly': readonly }]"
      @input="$emit('update:modelValue', ($event.target as HTMLInputElement).value)"
    />
    <Transition name="fade">
      <span v-if="error" class="base-input__error">{{ error }}</span>
    </Transition>
  </div>
</template>

<style scoped>
.base-input {
  display: flex;
  flex-direction: column;
  gap: var(--space-xs);
}

.base-input__label {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
}

.base-input__field {
  padding: 7px 10px;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  background: var(--bg-input);
  color: var(--text-primary);
  outline: none;
  transition: border-color var(--transition-fast), box-shadow var(--transition-fast);
}

.base-input__field:focus {
  border-color: var(--border-focus);
  box-shadow: var(--shadow-focus);
}

.base-input__field:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.base-input__field--readonly {
  background: var(--bg-secondary);
  cursor: default;
}

.base-input__field--error {
  border-color: var(--border-error);
}

.base-input__error {
  font-size: 12px;
  color: var(--color-error-text);
}
</style>
