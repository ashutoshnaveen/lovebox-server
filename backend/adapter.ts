export interface LoveboxMessage {
  id: string;
  deviceId: string;
  senderName: string;
  caption: string;
  imageId: string;
  imageSize: number;
  audioId?: string;
  audioSize?: number;
  createdAt: string;
  acknowledgedAt?: string;
}

export interface LoveboxFeedback {
  id: string;
  deviceId: string;
  messageId: string;
  type: 'like' | 'draw';
  imageId?: string;
  likedAt?: string;
  drawnAt?: string;
  createdAt: string;
}

export interface LoveboxHealth {
  deviceId: string;
  displayReady: boolean;
  touchReady: boolean;
  audioReady: boolean;
  servoReady: boolean;
  storageReady: boolean;
  mqttConnected: boolean;
  wifiRssi: number;
  freeHeap: number;
  uptimeMs: number;
  lastAudioPeak: number;
  lastError: string | null;
  lastSeenAt: string;
}

export interface SendMessageResult {
  ok: boolean;
  data?: LoveboxMessage;
  error?: string;
}

export interface BackendAdapter {
  sendMessage(deviceId: string, message: LoveboxMessage, imageBuffer: ArrayBuffer): Promise<void>;
  getLatestMessage(deviceId: string): Promise<LoveboxMessage | null>;
  getImage(deviceId: string, imageId: string): Promise<ArrayBuffer | null>;
  saveAudio(audioId: string, buffer: ArrayBuffer): Promise<void>;
  getAudio(audioId: string): Promise<ArrayBuffer | null>;
  saveFeedback(feedback: LoveboxFeedback): Promise<void>;
  getFeedbackHistory(deviceId: string, limit?: number): Promise<LoveboxFeedback[]>;
  getFeedbackImage(imageId: string): Promise<ArrayBuffer | null>;
  saveDeviceHealth(health: LoveboxHealth): Promise<void>;
  getDeviceHealth(deviceId: string): Promise<LoveboxHealth | null>;
  acknowledgeMessage(deviceId: string, acknowledgedAt: string): Promise<void>;
}
