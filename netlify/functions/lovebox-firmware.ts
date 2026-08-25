import { validateDeviceKey } from "../lib/auth";
import { sanitizeDeviceId } from "../lib/validation";

const BACKEND_VERSION = "1.0.3";

export default async (request: Request): Promise<Response> => {
  if (request.method !== "GET") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  if (!deviceId || !validateDeviceKey(deviceId, request.headers.get("X-Device-Key"))) {
    return jsonResponse({ ok: false, error: "Invalid or missing device credentials" }, 401);
  }

  const version = process.env.FIRMWARE_VERSION_LATEST;
  const firmwareUrl = process.env.FIRMWARE_BINARY_URL;
  const sha256 = process.env.FIRMWARE_SHA256;
  if (!version || !firmwareUrl || !sha256) {
    return jsonResponse({ ok: true, backendVersion: BACKEND_VERSION, data: null }, 200);
  }

  if (!firmwareUrl.startsWith("https://") || !/^\d+\.\d+\.\d+$/.test(version) || !/^[a-fA-F0-9]{64}$/.test(sha256)) {
    console.error("Invalid firmware deployment configuration");
    return jsonResponse({ ok: false, error: "Invalid firmware deployment configuration" }, 500);
  }

  return jsonResponse({
    ok: true,
    backendVersion: BACKEND_VERSION,
    data: { version, url: firmwareUrl, sha256: sha256.toLowerCase() },
  }, 200);
};

function jsonResponse(body: object, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}
