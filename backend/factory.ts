import type { BackendAdapter } from "./adapter.js";
import { createLocalAdapter } from "./adapters/local.js";
import { createFirebaseAdapter } from "./adapters/firebase.js";

export function createBackendAdapter(): BackendAdapter {
  const backendType = process.env.LOVEBOX_BACKEND || "local";

  switch (backendType) {
    case "local":
      return createLocalAdapter();
    case "firebase":
      return createFirebaseAdapter();
    default:
      throw new Error(`Unknown LOVEBOX_BACKEND: ${backendType}`);
  }
}
