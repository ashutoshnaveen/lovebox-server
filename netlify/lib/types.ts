export interface LoveboxMessage {
  id: string;
  deviceId: string;
  senderName: string;
  caption: string;
  imageId: string;
  imageSize: number;
  createdAt: string;
  acknowledgedAt?: string;
}

export interface LoveboxFeedback {
  id: string;
  deviceId: string;
  messageId: string;
  type: "like" | "draw";
  imageId?: string;
  likedAt?: string;
  drawnAt?: string;
  createdAt: string;
}

export interface LoveboxHealth {
  deviceId: string;
  firmwareVersion: string;
  uptimeMs: number;
  wifiRssi: number;
  freeHeap: number;
  psramTotal: number;
  psramFree: number;
  ffatMounted: boolean;
  ffatTotal: number;
  ffatUsed: number;
  resetReason: number;
  lastSuccessfulCommunicationMs: number;
  lastMessageId: string;
  displayReady: boolean;
  touchReady: boolean;
  audioReady: boolean;
  servoReady: boolean;
  reportedAt: string;
}

export interface JsonResponse<T> {
  ok: boolean;
  data?: T;
  error?: string;
}
