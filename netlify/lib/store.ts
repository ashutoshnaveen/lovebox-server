import { getStore } from "@netlify/blobs";
import type { LoveboxFeedback, LoveboxMessage } from "./types.ts";

function messagesStore() {
  return getStore({ name: "lovebox-messages", consistency: "strong" });
}

function imagesStore() {
  return getStore({ name: "lovebox-images", consistency: "strong" });
}

function feedbackStore() {
  return getStore({ name: "lovebox-feedback", consistency: "strong" });
}

function feedbackImagesStore() {
  return getStore({ name: "lovebox-feedback-images", consistency: "strong" });
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

export async function saveFeedback(feedback: LoveboxFeedback): Promise<void> {
  const store = feedbackStore();
  const eventKey = `events:${feedback.deviceId}:${feedback.createdAt}:${feedback.id}`;
  await Promise.all([
    store.setJSON(`latest:${feedback.deviceId}`, feedback),
    store.setJSON(eventKey, feedback),
  ]);
}

export async function getLatestFeedback(deviceId: string): Promise<LoveboxFeedback | null> {
  const store = feedbackStore();
  return (await store.get(`latest:${deviceId}`, { type: "json" })) as LoveboxFeedback | null;
}

export async function getFeedbackHistory(deviceId: string, limit = 20): Promise<LoveboxFeedback[]> {
  const store = feedbackStore();
  const { blobs } = await store.list({ prefix: `events:${deviceId}:` });
  const keys = blobs.map((blob) => blob.key).sort().reverse().slice(0, limit);
  const events = await Promise.all(keys.map((key) => store.get(key, { type: "json" })));
  return events as LoveboxFeedback[];
}

export async function saveFeedbackImage(imageId: string, buffer: Buffer): Promise<void> {
  const store = feedbackImagesStore();
  await store.set(`image:${imageId}`, buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer);
}

export async function getFeedbackImage(imageId: string): Promise<Buffer | null> {
  const store = feedbackImagesStore();
  const data = await store.get(`image:${imageId}`, { type: "arrayBuffer" });
  return data ? Buffer.from(data) : null;
}
