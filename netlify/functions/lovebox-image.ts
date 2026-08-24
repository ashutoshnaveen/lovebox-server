import { validateDeviceKey } from "../lib/auth";
import { getImage } from "../lib/store";
import { sanitizeDeviceId } from "../lib/validation";

export default async (request: Request): Promise<Response> => {
  if (request.method !== "GET") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  const imageId = url.searchParams.get("imageId");

  if (!deviceId || !imageId) {
    return jsonResponse({ ok: false, error: "Invalid or missing deviceId/imageId" }, 400);
  }

  const deviceKey = request.headers.get("X-Device-Key");
  if (!validateDeviceKey(deviceId, deviceKey)) {
    return jsonResponse({ ok: false, error: "Invalid or missing device key" }, 401);
  }

  try {
    const image = await getImage(deviceId, imageId);
    if (!image) {
      return jsonResponse({ ok: false, error: "Image not found" }, 404);
    }

    return new Response(image, {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Length": image.length.toString(),
        "Cache-Control": "no-store",
      },
    });
  } catch (error) {
    console.error("lovebox-image error:", error);
    return jsonResponse({ ok: false, error: "Failed to fetch image" }, 500);
  }
};

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
