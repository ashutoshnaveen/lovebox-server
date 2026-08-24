import { validateDeviceKey } from "../lib/auth";
import { acknowledgeMessage } from "../lib/store";
import { sanitizeDeviceId } from "../lib/validation";

export default async (request: Request): Promise<Response> => {
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

  try {
    await acknowledgeMessage(deviceId, new Date().toISOString());
    return jsonResponse({ ok: true }, 200);
  } catch (error) {
    console.error("lovebox-ack error:", error);
    return jsonResponse({ ok: false, error: "Failed to acknowledge" }, 500);
  }
};

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
