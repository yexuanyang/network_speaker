<template>
  <div class="app-header card-stagger">
    <h1 class="app-header__title">Network Speaker</h1>
    <p class="app-header__description">
      Configure and control audio streaming to network devices. Start and stop
      desktop audio forwarding from a single window.
    </p>
  </div>
</template>

<style scoped>
.app-header {
  padding: var(--space-lg);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-md);
  background: var(--bg-card);
  box-shadow: var(--shadow-card);
}

.app-header__title {
  font-size: 22px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.app-header__description {
  margin-top: var(--space-sm);
  color: var(--text-secondary);
  font-size: 13px;
  line-height: 1.6;
}
</style>
