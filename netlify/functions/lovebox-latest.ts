import { validateDeviceKey } from "../lib/auth";
import { getLatestMessage } from "../lib/store";
import { sanitizeDeviceId } from "../lib/validation";

export default async (request: Request): Promise<Response> => {
  if (request.method !== "GET") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  if (!deviceId) {
    return jsonResponse({ ok: false, error: "Invalid or missing deviceId" }, 400);
  }

  const deviceKey = request.headers.get("X-Device-Key");
  if (!validateDeviceKey(deviceId, deviceKey)) {
    return jsonResponse({ ok: false, error: "Invalid or missing device key" }, 401);
  }

  try {
    const message = await getLatestMessage(deviceId);
    if (!message) {
      return jsonResponse({ ok: true, data: null }, 200);
    }
    return jsonResponse({ ok: true, data: message }, 200);
  } catch (error) {
    console.error("lovebox-latest error:", error);
    return jsonResponse({ ok: false, error: "Failed to fetch message" }, 500);
  }
};

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
