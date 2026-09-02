import fs from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";
import type {
  BackendAdapter,
  LoveboxMessage,
  LoveboxFeedback,
  LoveboxHealth,
} from "./adapter.js";

const DATA_DIR = path.resolve("backend-data");

async function ensureDir(filePath: string) {
  const dir = path.dirname(filePath);
  await fs.mkdir(dir, { recursive: true });
}

export function createLocalAdapter(): BackendAdapter {
  return {
    async sendMessage(deviceId, message, imageBuffer) {
      const msgPath = path.join(DATA_DIR, "messages", `${deviceId}.json`);
      const imgPath = path.join(DATA_DIR, "images", `${deviceId}-${message.imageId}.bin`);
      await ensureDir(msgPath);
      await ensureDir(imgPath);
      await fs.writeFile(msgPath, JSON.stringify(message, null, 2));
      await fs.writeFile(imgPath, Buffer.from(imageBuffer));
    },

    async getLatestMessage(deviceId) {
      const msgPath = path.join(DATA_DIR, "messages", `${deviceId}.json`);
      try {
        const raw = await fs.readFile(msgPath, "utf-8");
        return JSON.parse(raw) as LoveboxMessage;
      } catch {
        return null;
      }
    },

    async getImage(deviceId, imageId) {
      const imgPath = path.join(DATA_DIR, "images", `${deviceId}-${imageId}.bin`);
      try {
        return await fs.readFile(imgPath);
      } catch {
        return null;
      }
    },

    async saveAudio(audioId, buffer) {
      const audioPath = path.join(DATA_DIR, "audio", `${audioId}.bin`);
      await ensureDir(audioPath);
      await fs.writeFile(audioPath, Buffer.from(buffer));
    },

    async getAudio(audioId) {
      const audioPath = path.join(DATA_DIR, "audio", `${audioId}.bin`);
      try {
        return await fs.readFile(audioPath);
      } catch {
        return null;
      }
    },

    async saveFeedback(feedback) {
      const feedbackPath = path.join(DATA_DIR, "feedback", `${feedback.deviceId}.json`);
      await ensureDir(feedbackPath);
      let list: LoveboxFeedback[] = [];
      try {
        const raw = await fs.readFile(feedbackPath, "utf-8");
        list = JSON.parse(raw);
      } catch {
        // ignore
      }
      list.push(feedback);
      await fs.writeFile(feedbackPath, JSON.stringify(list, null, 2));
    },

    async getFeedbackHistory(deviceId, limit = 20) {
      const feedbackPath = path.join(DATA_DIR, "feedback", `${deviceId}.json`);
      try {
        const raw = await fs.readFile(feedbackPath, "utf-8");
        const list = JSON.parse(raw) as LoveboxFeedback[];
        return list.slice(-limit).reverse();
      } catch {
        return [];
      }
    },

    async getFeedbackImage(imageId) {
      const imgPath = path.join(DATA_DIR, "feedback-images", `${imageId}.bin`);
      try {
        return await fs.readFile(imgPath);
      } catch {
        return null;
      }
    },

    async saveDeviceHealth(health) {
      const healthPath = path.join(DATA_DIR, "health", `${health.deviceId}.json`);
      await ensureDir(healthPath);
      await fs.writeFile(healthPath, JSON.stringify(health, null, 2));
    },

    async getDeviceHealth(deviceId) {
      const healthPath = path.join(DATA_DIR, "health", `${deviceId}.json`);
      try {
        const raw = await fs.readFile(healthPath, "utf-8");
        return JSON.parse(raw) as LoveboxHealth;
      } catch {
        return null;
      }
    },

    async acknowledgeMessage(deviceId, acknowledgedAt) {
      const msgPath = path.join(DATA_DIR, "messages", `${deviceId}.json`);
      try {
        const raw = await fs.readFile(msgPath, "utf-8");
        const msg = JSON.parse(raw) as LoveboxMessage;
        msg.acknowledgedAt = acknowledgedAt;
        await fs.writeFile(msgPath, JSON.stringify(msg, null, 2));
      } catch {
        // ignore
      }
    },
  };
}
