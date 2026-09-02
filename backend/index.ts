export { createBackendAdapter } from "./factory.js";
export { createLocalAdapter } from "./adapters/local.js";
export { createFirebaseAdapter } from "./adapters/firebase.js";
export type {
  BackendAdapter,
  LoveboxMessage,
  LoveboxFeedback,
  LoveboxHealth,
  SendMessageResult,
} from "./adapter.js";
