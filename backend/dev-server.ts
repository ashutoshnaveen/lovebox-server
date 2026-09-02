import http from "node:http";
import { createBackendAdapter } from "./backend/index.js";

const adapter = createBackendAdapter();
const PORT = process.env.PORT ? Number(process.env.PORT) : 3000;

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url || "/", `http://${req.headers.host}`);

  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type, X-Device-Key, X-Lovebox-Passcode, X-Feedback-Type, X-Message-Id");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  try {
    if (url.pathname === "/.netlify/functions/lovebox-send" && req.method === "POST") {
      // Parse multipart form data manually for simplicity
      const chunks: Buffer[] = [];
      req.on("data", (chunk) => chunks.push(chunk));
      req.on("end", async () => {
        try {
          const body = Buffer.concat(chunks);
          // For now, accept raw binary for image and expect JSON metadata in headers
          const deviceId = url.searchParams.get("deviceId");
          if (!deviceId) {
            res.writeHead(400, { "Content-Type": "application/json" });
            res.end(JSON.stringify({ ok: false, error: "Invalid or missing deviceId" }));
            return;
          }

          const messageId = `msg-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
          const message = {
            id: messageId,
            deviceId,
            senderName: url.searchParams.get("senderName") || "",
            caption: url.searchParams.get("caption") || "",
            imageId: messageId,
            audioId: url.searchParams.get("audioId") || undefined,
            createdAt: new Date().toISOString(),
          };

          await adapter.sendMessage(deviceId, message, body.buffer);
          res.writeHead(200, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ ok: true, data: message }));
        } catch (err) {
          console.error("dev-server send error:", err);
          res.writeHead(500, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ ok: false, error: "Failed to process message" }));
        }
      });
      return;
    }

    if (url.pathname === "/.netlify/functions/lovebox-latest" && req.method === "GET") {
      const deviceId = url.searchParams.get("deviceId");
      if (!deviceId) {
        res.writeHead(400, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, error: "Invalid or missing deviceId" }));
        return;
      }
      const message = await adapter.getLatestMessage(deviceId);
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ ok: true, data: message }));
      return;
    }

    if (url.pathname === "/.netlify/functions/lovebox-image" && req.method === "GET") {
      const deviceId = url.searchParams.get("deviceId");
      const imageId = url.searchParams.get("imageId");
      if (!deviceId || !imageId) {
        res.writeHead(400, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, error: "Invalid parameters" }));
        return;
      }
      const image = await adapter.getImage(deviceId, imageId);
      if (!image) {
        res.writeHead(404, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, error: "Image not found" }));
        return;
      }
      res.writeHead(200, { "Content-Type": "application/octet-stream" });
      res.end(image);
      return;
    }

    if (url.pathname === "/.netlify/functions/lovebox-audio" && req.method === "GET") {
      const deviceId = url.searchParams.get("deviceId");
      const audioId = url.searchParams.get("audioId");
      if (!deviceId || !audioId) {
        res.writeHead(400, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, error: "Invalid parameters" }));
        return;
      }
      const audio = await adapter.getAudio(audioId);
      if (!audio) {
        res.writeHead(404, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: false, error: "Audio not found" }));
        return;
      }
      res.writeHead(200, { "Content-Type": "audio/wav" });
      res.end(audio);
      return;
    }

    res.writeHead(404, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ ok: false, error: "Not found" }));
  } catch (err) {
    console.error("dev-server error:", err);
    res.writeHead(500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ ok: false, error: "Internal server error" }));
  }
});

server.listen(PORT, () => {
  console.log(`Dev server running on http://localhost:${PORT}`);
  console.log(`Backend: ${process.env.LOVEBOX_BACKEND || "local"}`);
});
