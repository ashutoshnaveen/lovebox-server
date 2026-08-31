import { validateDeviceKey } from "../lib/auth";
import { getAudio } from "../lib/store";
import { sanitizeDeviceId } from "../lib/validation";

export default async (request: Request): Promise<Response> => {
  if (request.method !== "GET") {
    return jsonResponse({ ok: false, error: "Method not allowed" }, 405);
  }

  const url = new URL(request.url);
  const deviceId = sanitizeDeviceId(url.searchParams.get("deviceId"));
  const audioId = url.searchParams.get("audioId");

  if (!deviceId || !audioId) {
    return jsonResponse({ ok: false, error: "Invalid or missing deviceId/audioId" }, 400);
  }

  const deviceKey = request.headers.get("X-Device-Key");
  if (!validateDeviceKey(deviceId, deviceKey)) {
    return jsonResponse({ ok: false, error: "Invalid or missing device key" }, 401);
  }

  try {
    const audio = await getAudio(audioId);
    if (!audio) {
      return jsonResponse({ ok: false, error: "Audio not found" }, 404);
    }

    return new Response(audio, {
      status: 200,
      headers: {
        "Content-Type": "audio/wav",
        "Content-Length": audio.length.toString(),
        "Cache-Control": "no-store",
      },
    });
  } catch (error) {
    console.error("lovebox-audio error:", error);
    return jsonResponse({ ok: false, error: "Failed to fetch audio" }, 500);
  }
};

function jsonResponse(body: object, status: number): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
