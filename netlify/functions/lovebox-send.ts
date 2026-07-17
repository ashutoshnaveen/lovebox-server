import sharp from "sharp";
import { validateSenderPasscode } from "../lib/auth";
import {
  generateId,
  sanitizeCaption,
  sanitizeDeviceId,
  sanitizeSenderName,
  MAX_IMAGE_SIZE,
} from "../lib/validation";
import { saveMessage } from "../lib/store";
import type { LoveboxMessage } from "../lib/types";

const DISPLAY_WIDTH = 320;
const DISPLAY_HEIGHT = 240;

export default async (request: Request): Promise<Response> => {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const passcode = request.headers.get("X-Lovebox-Passcode");
  if (!validateSenderPasscode(passcode)) {
    return jsonResponse({ ok: false, error: "Invalid or missing passcode" }, 401);
  }

  try {
    const formData = await request.formData();
    const deviceIdRaw = formData.get("deviceId");
    const imageFile = formData.get("image");
    const senderName = sanitizeSenderName(formData.get("senderName") as string | null);
    const caption = sanitizeCaption(formData.get("caption") as string | null);

    const deviceId = sanitizeDeviceId(deviceIdRaw as string | null);
    if (!deviceId) {
      return jsonResponse({ ok: false, error: "Invalid or missing deviceId" }, 400);
    }

    if (!imageFile || !(imageFile instanceof File) || imageFile.size === 0) {
      return jsonResponse({ ok: false, error: "Image is required" }, 400);
    }

    if (imageFile.size > MAX_IMAGE_SIZE) {
      return jsonResponse({ ok: false, error: "Image too large" }, 413);
    }

    const imageBuffer = Buffer.from(await imageFile.arrayBuffer());
    const resized = await sharp(imageBuffer)
      .resize(DISPLAY_WIDTH, DISPLAY_HEIGHT, { fit: "cover", position: "center" })
      .removeAlpha()
      .raw()
      .toBuffer();

    const rgb565 = convertToRgb565(resized);
    const imageId = generateId();

    const message: LoveboxMessage = {
      id: generateId(),
      deviceId,
      senderName,
      caption,
      imageId,
      imageSize: rgb565.length,
      createdAt: new Date().toISOString(),
    };

    await saveMessage(message, rgb565);

    return jsonResponse({ ok: true, data: message }, 200);
  } catch (error) {
    console.error("lovebox-send error:", error);
    return jsonResponse({ ok: false, error: "Failed to process image" }, 500);
  }
};

function convertToRgb565(rgbBuffer: Buffer): Buffer {
  const pixelCount = DISPLAY_WIDTH * DISPLAY_HEIGHT;
  const rgb565 = Buffer.alloc(pixelCount * 2);

  for (let i = 0; i < pixelCount; i++) {
    const r = rgbBuffer[i * 3];
    const g = rgbBuffer[i * 3 + 1];
    const b = rgbBuffer[i * 3 + 2];
    const color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    rgb565.writeUInt16LE(color, i * 2);
  }

  return rgb565;
}

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
