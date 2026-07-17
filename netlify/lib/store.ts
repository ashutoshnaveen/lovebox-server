import { getStore } from "@netlify/blobs";
import type { LoveboxMessage } from "./types.ts";

function messagesStore() {
  return getStore({ name: "lovebox-messages", consistency: "strong" });
}

function imagesStore() {
  return getStore({ name: "lovebox-images", consistency: "strong" });
}

export async function getLatestMessage(deviceId: string): Promise<LoveboxMessage | null> {
  const store = messagesStore();
  return (await store.get(`latest:${deviceId}`, { type: "json" })) as LoveboxMessage | null;
}

export async function saveMessage(message: LoveboxMessage, imageBuffer: Buffer): Promise<void> {
  const messages = messagesStore();
  const images = imagesStore();
  await Promise.all([
    messages.setJSON(`latest:${message.deviceId}`, message),
    images.set(`image:${message.deviceId}:${message.imageId}`, imageBuffer.buffer.slice(imageBuffer.byteOffset, imageBuffer.byteOffset + imageBuffer.byteLength) as ArrayBuffer),
  ]);
}

export async function getImage(deviceId: string, imageId: string): Promise<Buffer | null> {
  const store = imagesStore();
  const data = await store.get(`image:${deviceId}:${imageId}`, { type: "arrayBuffer" });
  return data ? Buffer.from(data) : null;
}

export async function acknowledgeMessage(deviceId: string, acknowledgedAt: string): Promise<void> {
  const store = messagesStore();
  const message = await getLatestMessage(deviceId);
  if (!message) return;
  message.acknowledgedAt = acknowledgedAt;
  await store.setJSON(`latest:${message.deviceId}`, message);
}
