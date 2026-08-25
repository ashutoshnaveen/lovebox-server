# Lovebox API Contract

Base URL: `https://<site-name>.netlify.app`

All timestamps are ISO-8601 UTC strings.

## Endpoints

### 1. Device health

```
GET /.netlify/functions/lovebox-health?deviceId=lovebox-001
POST /.netlify/functions/lovebox-health?deviceId=lovebox-001
```

Both methods require `X-Device-Key`. `POST` stores the latest device report; `GET` retrieves it.

### 2. Firmware update manifest

```
GET /.netlify/functions/lovebox-firmware?deviceId=lovebox-001
```

This requires `X-Device-Key`. Set `FIRMWARE_VERSION_LATEST`, `FIRMWARE_BINARY_URL`, and `FIRMWARE_SHA256` in Netlify to publish an update. The device only installs a strictly newer semantic version after checking the HTTPS URL, size, and SHA-256 digest.

### 3. Send an image

```
POST /.netlify/functions/lovebox-send
```

Headers:
- `X-Lovebox-Passcode`: shared sender passcode

Body (multipart/form-data):
- `deviceId`: e.g. `lovebox-001`
- `image`: image file (JPEG/PNG, up to 10 MB)
- `senderName`: optional, max 60 chars
- `caption`: optional, max 120 chars

Response:
```json
{
  "ok": true,
  "data": {
    "id": "monotonic-id",
    "deviceId": "lovebox-001",
    "senderName": "Ashutosh",
    "caption": "Miss you!",
    "imageId": "image-id",
    "imageSize": 153600,
    "createdAt": "2026-07-17T..."
  }
}
```

### 3. Get latest image metadata

```
GET /.netlify/functions/lovebox-latest?deviceId=lovebox-001
```

Headers:
- `X-Device-Key`: per-device secret

Response:
```json
{
  "ok": true,
  "data": {
    "id": "monotonic-id",
    "deviceId": "lovebox-001",
    "senderName": "Ashutosh",
    "caption": "Miss you!",
    "imageId": "image-id",
    "imageSize": 153600,
    "createdAt": "2026-07-17T...",
    "acknowledgedAt": null
  }
}
```

If no image exists yet, `data` is `null`.

### 4. Download image binary

```
GET /.netlify/functions/lovebox-image?deviceId=lovebox-001&imageId=<imageId>
```

Headers:
- `X-Device-Key`: per-device secret

Response: raw RGB565 binary, 153,600 bytes (320×240 × 2 bytes).

Pixel format: little-endian uint16 RGB565, row-major, top-to-bottom.

### 5. Acknowledge delivery

```
POST /.netlify/functions/lovebox-ack?deviceId=lovebox-001
```

Headers:
- `X-Device-Key`: per-device secret

Response:
```json
{ "ok": true }
```

## curl examples

Health:
```bash
curl https://<site>.netlify.app/.netlify/functions/lovebox-health?deviceId=lovebox-001 \
  -H "X-Device-Key: your-device-key"
```

Send image:
```bash
curl -X POST \
  https://<site>.netlify.app/.netlify/functions/lovebox-send \
  -H "X-Lovebox-Passcode: your-passcode" \
  -F "deviceId=lovebox-001" \
  -F "senderName=Ashutosh" \
  -F "caption=Miss you!" \
  -F "image=@photo.jpg"
```

Get latest metadata:
```bash
curl "https://<site>.netlify.app/.netlify/functions/lovebox-latest?deviceId=lovebox-001" \
  -H "X-Device-Key: your-device-key"
```

Download image binary:
```bash
curl "https://<site>.netlify.app/.netlify/functions/lovebox-image?deviceId=lovebox-001&imageId=IMAGE_ID" \
  -H "X-Device-Key: your-device-key" \
  --output image.bin
```
