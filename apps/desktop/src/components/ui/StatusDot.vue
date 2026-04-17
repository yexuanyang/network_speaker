<script setup lang="ts">
defineProps<{
  color: "green" | "yellow" | "red";
}>();
</script>

<template>
  <span :class="['status-dot', `status-dot--${color}`]" />
</template>

<style scoped>
.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  flex-shrink: 0;
  transition: background-color var(--transition-normal);
}

.status-dot--green {
  background-color: var(--color-success);
}

.status-dot--yellow {
  background-color: var(--color-warning);
}

.status-dot--red {
  background-color: var(--color-error);
}
</style>
