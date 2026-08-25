import { validateDeviceKey, validateSenderPasscode } from "../lib/auth";
import { getDeviceHealth, saveDeviceHealth } from "../lib/store";
import { sanitizeDeviceId } from "../lib/validation";

const BACKEND_VERSION = "1.0.2";

export default async (request: Request): Promise<Response> => {
  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  if (request.method === "GET") {
    if (!deviceId || !validateSenderPasscode(request.headers.get("X-Lovebox-Passcode"))) {
      return jsonResponse({ ok: false, error: "Invalid or missing sender credentials" }, 401);
    }
    const health = await getDeviceHealth(deviceId);
    return jsonResponse({ ok: true, backendVersion: BACKEND_VERSION, data: health }, 200);
  }

  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  if (!deviceId || !validateDeviceKey(deviceId, request.headers.get("X-Device-Key"))) {
    return jsonResponse({ ok: false, error: "Invalid or missing device credentials" }, 401);
  }

  try {
    const body: unknown = await request.json();
    if (!body || typeof body !== "object" || Array.isArray(body)) {
      throw new Error("Health report must be a JSON object");
    }
    const health = normalizeHealth(body as Record<string, unknown>, deviceId);
    await saveDeviceHealth(health);
    return jsonResponse({ ok: true, backendVersion: BACKEND_VERSION, data: health }, 200);
  } catch (error) {
    console.error("lovebox-health error:", error);
    return jsonResponse({ ok: false, error: "Invalid health report" }, 400);
  }
};

function normalizeHealth(body: Record<string, unknown>, deviceId: string) {
  const numberField = (name: string) => typeof body[name] === "number" && Number.isFinite(body[name]) ? body[name] as number : 0;
  const booleanField = (name: string) => body[name] === true;
  const stringField = (name: string) => typeof body[name] === "string" ? body[name] as string : "";

  return {
    deviceId,
    firmwareVersion: stringField("firmwareVersion"),
    uptimeMs: numberField("uptimeMs"),
    wifiRssi: numberField("wifiRssi"),
    freeHeap: numberField("freeHeap"),
    psramTotal: numberField("psramTotal"),
    psramFree: numberField("psramFree"),
    ffatMounted: booleanField("ffatMounted"),
    ffatTotal: numberField("ffatTotal"),
    ffatUsed: numberField("ffatUsed"),
    resetReason: numberField("resetReason"),
    lastSuccessfulCommunicationMs: numberField("lastSuccessfulCommunicationMs"),
    lastMessageId: stringField("lastMessageId"),
    displayReady: booleanField("displayReady"),
    touchReady: booleanField("touchReady"),
    audioReady: booleanField("audioReady"),
    servoReady: booleanField("servoReady"),
    reportedAt: new Date().toISOString(),
  };
}

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
