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

export interface JsonResponse<T> {
  ok: boolean;
  data?: T;
  error?: string;
}
