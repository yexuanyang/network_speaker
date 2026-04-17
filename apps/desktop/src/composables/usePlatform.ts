import { ref } from "vue";
import type { PlatformInfo } from "@/types";
import { getPlatformInfo } from "@/lib/tauri";

const platform = ref<PlatformInfo | null>(null);

export function usePlatform() {
  async function initialize() {
    if (!platform.value) {
      platform.value = await getPlatformInfo();
    }
  }

  return { platform, initialize };
}
