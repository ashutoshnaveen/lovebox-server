import { Jimp } from "jimp";
import { validateDeviceKey, validateSenderPasscode } from "../lib/auth";
import {
  getFeedbackImage,
  getLatestFeedback,
  saveFeedback,
  saveFeedbackImage,
} from "../lib/store";
import { generateId } from "../lib/validation";
import { sanitizeDeviceId } from "../lib/validation";
import type { LoveboxFeedback } from "../lib/types";

const DISPLAY_WIDTH = 320;
const DISPLAY_HEIGHT = 240;

export default async (request: Request): Promise<Response> => {
  if (request.method === "GET") {
    return handleGet(request);
  }
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const deviceKey = request.headers.get("X-Device-Key");
  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));

  if (!deviceId) {
    return jsonResponse({ ok: false, error: "Invalid or missing deviceId" }, 400);
  }

  if (!validateDeviceKey(deviceId, deviceKey)) {
    return jsonResponse({ ok: false, error: "Invalid or missing device key" }, 401);
  }

  const feedbackType = request.headers.get("X-Feedback-Type") || "";

  if (feedbackType === "draw") {
    return handleDrawFeedback(request, deviceId);
  } else if (feedbackType === "like") {
    return handleLikeFeedback(request, deviceId);
  } else {
    return jsonResponse({ ok: false, error: "Invalid feedback type" }, 400);
  }
};

async function handleLikeFeedback(request: Request, deviceId: string) {
  try {
    const body = (await request.json()) as { messageId?: string };
    const messageId = String(body.messageId || "");
    if (!messageId) {
      return jsonResponse({ ok: false, error: "Missing messageId" }, 400);
    }

    const feedback: LoveboxFeedback = {
      id: generateId(),
      deviceId,
      messageId,
      type: "like",
      likedAt: new Date().toISOString(),
      createdAt: new Date().toISOString(),
    };
    await saveFeedback(feedback);
    return jsonResponse({ ok: true, data: feedback }, 200);
  } catch (error) {
    return jsonResponse({ ok: false, error: "Invalid JSON" }, 400);
  }
}

async function handleDrawFeedback(request: Request, deviceId: string) {
  const messageId = request.headers.get("X-Message-Id");
  if (!messageId) {
    return jsonResponse({ ok: false, error: "Missing X-Message-Id" }, 400);
  }

  const rgb565 = Buffer.from(await request.arrayBuffer());
  if (rgb565.length !== DISPLAY_WIDTH * DISPLAY_HEIGHT * 2) {
    return jsonResponse({ ok: false, error: "Invalid image size" }, 400);
  }

  const imageId = generateId();
  const png = await convertRgb565ToPng(rgb565);

  await Promise.all([
    saveFeedbackImage(imageId, png),
    saveFeedback({
      id: generateId(),
      deviceId,
      messageId,
      type: "draw",
      imageId,
      drawnAt: new Date().toISOString(),
      createdAt: new Date().toISOString(),
    }),
  ]);

  return jsonResponse({ ok: true, data: { imageId } }, 200);
}

async function convertRgb565ToPng(rgb565: Buffer): Promise<Buffer> {
  const image = new Jimp({ width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT });
  const data = image.bitmap.data;
  for (let i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
    const color = rgb565.readUInt16LE(i * 2);
    const r5 = (color >> 11) & 0x1F;
    const g6 = (color >> 5) & 0x3F;
    const b5 = color & 0x1F;
    const r = (r5 << 3) | (r5 >> 2);
    const g = (g6 << 2) | (g6 >> 4);
    const b = (b5 << 3) | (b5 >> 2);
    data[i * 4] = r;
    data[i * 4 + 1] = g;
    data[i * 4 + 2] = b;
    data[i * 4 + 3] = 255;
  }
  return image.getBuffer("image/png");
}

async function handleGet(request: Request) {
  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  if (!deviceId) {
    return jsonResponse({ ok: false, error: "Invalid or missing deviceId" }, 400);
  }

  const passcode = request.headers.get("X-Lovebox-Passcode");
  if (!validateSenderPasscode(passcode)) {
    return jsonResponse({ ok: false, error: "Invalid or missing passcode" }, 401);
  }

  const feedback = await getLatestFeedback(deviceId);
  if (!feedback) {
    return jsonResponse({ ok: true, data: null }, 200);
  }

  let imageData: string | null = null;
  if (feedback.imageId) {
    const image = await getFeedbackImage(feedback.imageId);
    if (image) {
      imageData = `data:image/png;base64,${image.toString("base64")}`;
    }
  }

  return jsonResponse({ ok: true, data: feedback, imageData }, 200);
}

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
