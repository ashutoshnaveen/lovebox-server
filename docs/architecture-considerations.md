# Architecture Considerations & Open Issues

## Current Issues

1. **Audio volume is very low**
   - ESP32 I2S playback to MAX98357A is weak; current digital gain (~1.6x) insufficient.
   - Likely causes: I2S level/bias, mono-to-stereo duplication halving amplitude, MAX98357A gain pin default, or missing amplification stage.
   - Fix ideas: increase digital gain, verify GPIO/I2S wiring, consider line-level preamp or amplifier module, review WAV normalization in PWA.

2. **Netlify costs are growing quickly**
   - Each audio message downloads through Netlify Functions + KV store reads.
   - High-bandwidth audio traffic + function invocations increase bill.
   - Fix ideas: cache audio at edge/CDN, serve static audio from object storage, reduce function cold-start overhead, move media handling to dedicated object storage.

3. **Architecture needs careful review**
   - Current design couples Netlify Functions tightly to business logic and media serving.
   - Need clean separation between: auth, message metadata, media storage, device sync.
   - Consider event-driven or queue-based architecture for scalability.

## Platform Agnostic Design Goals

- **Storage backend** should be swappable (Netlify KV → S3/R2, GCS, etc.).
- **Compute layer** should be portable (Netlify Functions → AWS Lambda, Cloudflare Workers, Vercel Edge, etc.).
- **Device protocol** should remain stable so ESP32 firmware does not need rewrite during migration.
- **API contract** should be versioned and well-defined so frontends and backends can evolve independently.

## Proposed Architecture Refactor

### Storage Layer
```
MediaStore {
  upload(deviceId, file, metadata) -> { id, size, mime }
  get(deviceId, mediaId) -> ReadableStream | Buffer
  delete(deviceId, mediaId) -> void
}
```

### Compute Layer
```
MessageService {
  send(deviceId, metadata, media) -> Message
  latest(deviceId) -> Message | null
  ack(deviceId, messageId) -> void
}
```

### Device API
```
GET /.netlify/functions/lovebox-latest?deviceId=... (JSON metadata)
GET /.netlify/functions/lovebox-image?deviceId=...&imageId=... (binary)
GET /.netlify/functions/lovebox-audio?deviceId=...&audioId=... (binary)
POST /.netlify/functions/lovebox-send (multipart: image + audio)
POST /.netlify/functions/lovebox-ack (ack delivery)
```

## Next Steps

1. Measure current audio signal path with oscilloscope or debug logs.
2. Profile Netlify costs: per-function duration, bandwidth, cold starts.
3. Draft interface boundaries (`MediaStore`, `AuthProvider`, `MessageService`) before code changes.
4. Evaluate CDN + object storage costs vs Netlify Function + KV costs for audio assets.
