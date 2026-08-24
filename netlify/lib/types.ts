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

export interface JsonResponse<T> {
  ok: boolean;
  data?: T;
  error?: string;
}
