# Lovebox Server

Backend, PWA sender page, and ESP32 firmware for the Lovebox IoT project.

- **Sender page:** Mobile-first PWA for uploading photos.
- **Backend:** Netlify Functions + Netlify Blobs for image storage.
- **Device:** ESP32-S3 polls for new images, downloads a 320×240 RGB565 binary, and renders it on an ILI9341 TFT.

## Project structure

```
public/                  # PWA static files
netlify/functions/       # Serverless functions
netlify/lib/             # Shared utilities
firmware/lovebox_esp32/  # Arduino sketch
docs/                    # API + integration docs
```

## Local development

1. Install dependencies:
   ```bash
   npm install
   ```

2. Copy environment variables:
   ```bash
   cp .env.example .env
   ```
   Edit `.env` and set a strong `LOVEBOX_PASSCODE` and a random `DEVICE_KEY_LOVEBOX_001`.

3. Link the site to Netlify (needed for Netlify Blobs local access):
   ```bash
   npx netlify link
   ```

4. Start the local dev server:
   ```bash
   npm run dev
   ```

   The PWA is at `http://localhost:8888` and functions are at `http://localhost:8888/.netlify/functions/<name>`.

## Deploy to Netlify

1. Push the repo to GitHub (already done via `gh`).
2. In the Netlify dashboard, click **Add new site → Import an existing project** and select `lovebox-server`.
3. Add the environment variables in **Site settings → Environment variables**:
   - `LOVEBOX_PASSCODE`
   - `DEVICE_KEY_LOVEBOX_001`
4. Deploy. Netlify will run `npm run build` and publish `dist/`.

The site is available at `https://effervescent-scone-29511f.netlify.app`.

## Custom domain later

If you want `lovebox.ashutoshnaveen.com` later, add this CNAME in GoDaddy:

| Type | Name | Value |
|---|---|---|
| CNAME | `lovebox` | `<your-netlify-site-name>.netlify.app` |

Then add the custom domain in the Netlify site settings. SSL is provisioned automatically.

## Test commands

See [`docs/lovebox-api.md`](docs/lovebox-api.md) for full curl examples.

Quick health check:
```bash
curl https://<site-name>.netlify.app/.netlify/functions/lovebox-health
```

## ESP32 firmware

See [`docs/esp32-integration.md`](docs/esp32-integration.md) for wiring, library setup, and flashing instructions.
