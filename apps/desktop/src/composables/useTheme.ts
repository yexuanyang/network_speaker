import { ref, onMounted, onUnmounted } from "vue";

export function useTheme() {
  const isDark = ref(false);

  let mediaQuery: MediaQueryList | null = null;

  const update = () => {
    isDark.value = mediaQuery?.matches ?? false;
    document.documentElement.setAttribute(
      "data-theme",
      isDark.value ? "dark" : "light"
    );
  };

  onMounted(() => {
    mediaQuery = window.matchMedia("(prefers-color-scheme: dark)");
    update();
    mediaQuery.addEventListener("change", update);
  });

  onUnmounted(() => {
    mediaQuery?.removeEventListener("change", update);
  });

  return { isDark };
}
