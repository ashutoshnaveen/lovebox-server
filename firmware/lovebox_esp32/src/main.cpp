/************************************************************
  Lovebox ESP32-S3 Firmware

  - Connects via WiFiManager captive portal
  - Polls Netlify backend for new images
  - Downloads 320x240 RGB565 binary and renders it on ILI9341
  - Animates SG90 servo on new image
  - Stores last processed image ID in NVS (Preferences)
  - XPT2046 touchscreen: heart like, pen toolbar, drawing feedback

  Required libraries:
    - LovyanGFX
    - XPT2046_Touchscreen
    - WiFiManager
    - ArduinoJson
    - ESP32Servo
************************************************************/

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FFat.h>
#include <ESP32Servo.h>
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <Update.h>
#include <mbedtls/sha256.h>

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------
const char* DEVICE_ID = "lovebox-001";
const char* DEVICE_KEY = "32b65c99d66ee1a4093e214ce55bc786495bb1421140ae268d7f0b3fdbab6730";
const char* API_HOST = "https://effervescent-scone-29511f.netlify.app";
const char* FIRMWARE_VERSION = "1.0.3";

// ---------------- TFT pins ----------------
#undef TFT_CS
#undef TFT_DC
#undef TFT_RST
#undef TFT_MOSI
#undef TFT_MISO
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST   14
#define TFT_MOSI  11
#define TFT_SCK   12
#define TFT_MISO  13

// ---------------- Touch pins (shared SPI bus, separate CS) ----------------
#define TOUCH_CS   16
#define TOUCH_IRQ  17
#define TOUCH_MOSI 11
#define TOUCH_MISO 13
#define TOUCH_SCK  12

// Calibration for current 320x240 landscape orientation (tft.setRotation(1))
const int RAW_X_MIN = 439;
const int RAW_X_MAX = 3867;
const int RAW_Y_MIN = 360;
const int RAW_Y_MAX = 3795;

// Set true only once when you deliberately want to erase saved Wi-Fi
#define ERASE_SAVED_WIFI false

const int POLL_INTERVAL_MS = 5000;
const int HTTP_TIMEOUT_MS = 20000;
const int DOWNLOAD_TIMEOUT_MS = 30000;
const unsigned long HEALTH_INTERVAL_MS = 15UL * 60UL * 1000UL;
const unsigned long OTA_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
const size_t OTA_MAX_IMAGE_SIZE = 1310720;

const int SERVO_PIN = 15;
const int SERVO_BASE_ANGLE = 90;
const int SERVO_LEFT_ANGLE = 45;
const int SERVO_RIGHT_ANGLE = 135;
const int SERVO_STEP_DELAY_MS = 20;

const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;
const int IMAGE_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT * 2; // RGB565, 2 bytes per pixel
const char* IMAGE_PATH = "/latest.rgb565";

class LoveboxDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel;
  lgfx::Bus_SPI bus;

 public:
  LoveboxDisplay() {
    auto busConfig = bus.config();
    busConfig.spi_host = SPI2_HOST;
    busConfig.spi_mode = 0;
    busConfig.freq_write = 10000000;
    busConfig.freq_read = 16000000;
    busConfig.spi_3wire = false;
    busConfig.use_lock = true;
    busConfig.dma_channel = SPI_DMA_CH_AUTO;
    busConfig.pin_sclk = TFT_SCK;
    busConfig.pin_mosi = TFT_MOSI;
    busConfig.pin_miso = TFT_MISO;
    busConfig.pin_dc = TFT_DC;
    bus.config(busConfig);
    panel.setBus(&bus);

    auto panelConfig = panel.config();
    panelConfig.pin_cs = TFT_CS;
    panelConfig.pin_rst = TFT_RST;
    panelConfig.pin_busy = -1;
    panelConfig.panel_width = 240;
    panelConfig.panel_height = 320;
    panelConfig.offset_x = 0;
    panelConfig.offset_y = 0;
    panelConfig.offset_rotation = 0;
    panelConfig.invert = false;
    panelConfig.rgb_order = false;
    panelConfig.dlen_16bit = false;
    panelConfig.bus_shared = false;
    panel.config(panelConfig);
    setPanel(&panel);
  }
};

#define ILI9341_BLACK TFT_BLACK
#define ILI9341_WHITE TFT_WHITE
#define ILI9341_RED TFT_RED
#define ILI9341_GREEN TFT_GREEN
#define ILI9341_BLUE TFT_BLUE
#define ILI9341_YELLOW TFT_YELLOW
#define ILI9341_PINK 0xFC18

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------
LoveboxDisplay tft;
Servo heartServo;
WiFiClientSecure secureClient;
HTTPClient http;
Preferences prefs;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

String lastProcessedId;
String lastImageError;
String currentCaption;
bool imageStorageReady = false;
bool displayReady = false;
bool servoReady = false;
unsigned long lastHealthAt = 0;
unsigned long lastSuccessfulCommunicationAt = 0;
unsigned long lastOtaCheckAt = 0;

struct LatestMessage {
  String id;
  String imageId;
  String caption;
  String senderName;
  bool valid;
};

// ---------------------------------------------------------------------------
// Touch overlay / UI state
// ---------------------------------------------------------------------------
struct Button {
  int x, y, w, h;
};

const Button heartBtn = { 250, 0, 70, 40 };
const Button penBtn = { 0, 200, 60, 40 };

const int TOOLBAR_Y = 195;
const int TOOLBAR_H = 45;
const Button clearBtn = { 5, 200, 75, 35 };
const Button sendBtn = { 90, 200, 75, 35 };
const Button closeBtn = { 245, 200, 70, 35 };

// Color swatches shown above the toolbar when the pen is active
struct ColorSwatch {
  int x, y, w, h;
  uint16_t color;
};

const int SWATCH_COUNT = 5;
const ColorSwatch swatches[SWATCH_COUNT] = {
  { 10, 160, 40, 28, ILI9341_WHITE },
  { 60, 160, 40, 28, ILI9341_RED },
  { 110, 160, 40, 28, ILI9341_YELLOW },
  { 160, 160, 40, 28, ILI9341_GREEN },
  { 210, 160, 40, 28, ILI9341_BLUE },
};
int activeColorIndex = 0;

// 1-bit overlay: 320 * 240 / 8 = 9600 bytes
uint8_t overlayBuffer[(SCREEN_WIDTH * SCREEN_HEIGHT) / 8];

bool toolbarVisible = false;
bool drawModeActive = false;
bool overlayHasStrokes = false;   // True once at least one pixel is drawn
bool strokeButtonsShown = false;  // True after CLR/SEND have been rendered

bool touchReady = false;
bool wasTouched = false;
int touchStartX = 0;
int touchStartY = 0;
int touchLastX = 0;
int touchLastY = 0;
bool touchMoved = false;
int touchDownSamples = 0;          // Samples seen since contact began
unsigned long touchUpAt = 0;       // Time of last release (debounce)
const int TOUCH_SETTLE_SAMPLES = 3;          // Discard noisy first samples
const unsigned long TOUCH_DEBOUNCE_MS = 60;  // Ignore taps right after release
unsigned long lastPollAt = 0;

String toastText;
unsigned long toastUntil = 0;

// Forward declarations
void displayCaption();
LatestMessage fetchLatestMessage();
bool downloadAndDisplayImage(const String& imageId);
void sendAck();
void animateHeart();
void moveServo(int fromAngle, int toAngle);
void renderScreen();
void handleTap(int x, int y);
bool sendLikeFeedback();
bool sendDrawingFeedback();
bool sendHealthReport();
bool checkForFirmwareUpdate();

// ---------------------------------------------------------------------------
// Screen helpers
// ---------------------------------------------------------------------------
void showScreen(
  const String& heading,
  const String& line1 = "",
  const String& line2 = "",
  uint16_t headingColor = ILI9341_RED
) {
  tft.fillScreen(ILI9341_BLACK);

  tft.setTextWrap(true);

  tft.setTextColor(headingColor, ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 25);
  tft.println(heading);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);

  if (line1.length() > 0) {
    tft.setCursor(20, 95);
    tft.println(line1);
  }

  if (line2.length() > 0) {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(20, 155);
    tft.println(line2);
  }
}

bool displayStoredImage() {
  if (!imageStorageReady || !FFat.exists(IMAGE_PATH)) return false;

  File imageFile = FFat.open(IMAGE_PATH, FILE_READ);
  if (!imageFile || imageFile.size() != IMAGE_SIZE) {
    Serial.printf("Cached image invalid: %d bytes\n", imageFile ? imageFile.size() : -1);
    if (imageFile) imageFile.close();
    return false;
  }

  Serial.printf("Rendering cached image: %d bytes\n", imageFile.size());
  tft.setAddrWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.startWrite();

  const int CHUNK_PIXELS = 512;
  uint16_t pixels[CHUNK_PIXELS];
  int remaining = IMAGE_SIZE;

  while (remaining > 0) {
    int bytesRead = imageFile.read(reinterpret_cast<uint8_t*>(pixels), min(remaining, CHUNK_PIXELS * 2));
    if (bytesRead <= 0 || bytesRead % 2 != 0) break;

    tft.writePixels(pixels, bytesRead / 2, true);
    remaining -= bytesRead;
  }

  tft.endWrite();
  imageFile.close();
  return remaining == 0;
}

void configModeCallback(WiFiManager* wifiManager) {
  Serial.println("Wi-Fi setup portal started");

  String apName = wifiManager->getConfigPortalSSID();

  showScreen(
    "WI-FI SETUP",
    "Connect phone to:",
    apName,
    ILI9341_YELLOW
  );

  Serial.print("Setup network: ");
  Serial.println(apName);
  Serial.println("Open http://192.168.4.1 if portal does not open");
}

void saveConfigCallback() {
  Serial.println("New Wi-Fi credentials saved");

  showScreen(
    "SAVED!",
    "Wi-Fi details stored",
    "Restarting..."
  );
}

// ---------------------------------------------------------------------------
// Touch overlay helpers
// ---------------------------------------------------------------------------
int mapTouchToScreenX(int rawX) {
  return constrain(map(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
}

int mapTouchToScreenY(int rawY) {
  return constrain(map(rawY, RAW_Y_MIN, RAW_Y_MAX, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
}

bool isInButton(int x, int y, const Button& btn) {
  return x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h;
}

bool isInAnyControl(int x, int y) {
  if (isInButton(x, y, heartBtn)) return true;
  if (!toolbarVisible) {
    if (isInButton(x, y, penBtn)) return true;
  } else {
    for (int i = 0; i < SWATCH_COUNT; i++) {
      const ColorSwatch& s = swatches[i];
      if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) return true;
    }
    // CLR/SEND only active once something has been drawn
    if (overlayHasStrokes) {
      if (isInButton(x, y, clearBtn)) return true;
      if (isInButton(x, y, sendBtn)) return true;
    }
    if (isInButton(x, y, closeBtn)) return true;
  }
  return false;
}

bool getOverlayPixel(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return false;
  int idx = y * SCREEN_WIDTH + x;
  return overlayBuffer[idx >> 3] & (1 << (idx & 7));
}

void setOverlayPixel(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  int idx = y * SCREEN_WIDTH + x;
  int byteIdx = idx >> 3;
  uint8_t bit = 1 << (idx & 7);
  if (overlayBuffer[byteIdx] & bit) return;
  overlayBuffer[byteIdx] |= bit;
  overlayHasStrokes = true;
  tft.drawPixel(x, y, swatches[activeColorIndex].color);
}

void clearOverlay() {
  memset(overlayBuffer, 0, sizeof(overlayBuffer));
  overlayHasStrokes = false;
  strokeButtonsShown = false;
}

void drawLine(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  tft.startWrite();
  while (true) {
    setOverlayPixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
  tft.endWrite();
}

void resetFeedbackState() {
  clearOverlay();
  toolbarVisible = false;
  drawModeActive = false;
  strokeButtonsShown = false;
}// ---------------------------------------------------------------------------
// UI rendering
// ---------------------------------------------------------------------------
void drawButton(const Button& btn, uint16_t bg, uint16_t fg, const char* label) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, bg);
  tft.setTextColor(fg, bg);
  tft.setTextSize(2);  // Bold, clearly readable at 320x240
  int16_t w = strlen(label) * 12;  // 6px per char * scale 2
  tft.setCursor(btn.x + (btn.w - w) / 2, btn.y + (btn.h - 14) / 2);
  tft.print(label);
}

void drawHeartButton(const Button& btn) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, ILI9341_RED);
  int cx = btn.x + btn.w / 2;
  int cy = btn.y + btn.h / 2 - 1;
  tft.fillCircle(cx - 8, cy - 4, 7, ILI9341_WHITE);
  tft.fillCircle(cx + 8, cy - 4, 7, ILI9341_WHITE);
  tft.fillTriangle(cx - 15, cy - 2, cx + 15, cy - 2, cx, cy + 14, ILI9341_WHITE);
}

void renderUI() {
  drawHeartButton(heartBtn);

  if (!toolbarVisible) {
    // Blue background -> white text
    drawButton(penBtn, ILI9341_BLUE, ILI9341_WHITE, "PEN");
  } else {
    // Color swatches (active one gets a white outline)
    for (int i = 0; i < SWATCH_COUNT; i++) {
      const ColorSwatch& s = swatches[i];
      tft.fillRect(s.x, s.y, s.w, s.h, s.color);
      if (i == activeColorIndex) {
        tft.drawRect(s.x - 1, s.y - 1, s.w + 2, s.h + 2, ILI9341_WHITE);
      } else {
        tft.drawRect(s.x - 1, s.y - 1, s.w + 2, s.h + 2, ILI9341_BLACK);
      }
    }
    tft.fillRect(0, TOOLBAR_Y, SCREEN_WIDTH, TOOLBAR_H, ILI9341_BLACK);
    if (overlayHasStrokes) {
      // Dark grey background -> white text
      drawButton(clearBtn, 0x5A69, ILI9341_WHITE, "CLR");
      // Green background -> white text
      drawButton(sendBtn, ILI9341_GREEN, ILI9341_WHITE, "SEND");
    }
    // Red background -> white text
    drawButton(closeBtn, ILI9341_RED, ILI9341_WHITE, "CLOSE");
  }
}

void renderScreen() {
  displayStoredImage();
  displayCaption();
  renderUI();
}

void flashRect(const Button& btn) {
  tft.drawRect(btn.x - 2, btn.y - 2, btn.w + 4, btn.h + 4, ILI9341_WHITE);
  delay(120);
  renderUI();
}

void displayCaption() {
  if (currentCaption.length() == 0) return;
  const int capY = 42;
  const int capH = 22;
  tft.fillRect(0, capY, SCREEN_WIDTH, capH, ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(true);
  tft.setCursor(5, capY + 4);
  tft.print(currentCaption);
}

void showToast(const String& text) {
  toastText = text;
  toastUntil = millis() + 1500;
  tft.fillRect(60, 100, 200, 40, ILI9341_BLACK);
  tft.drawRect(60, 100, 200, 40, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(80, 113);
  tft.print(text);
}

void clearToast() {
  if (toastUntil > 0 && millis() > toastUntil) {
    toastUntil = 0;
    renderScreen();
  }
}

// ---------------------------------------------------------------------------
// Touch handling
// ---------------------------------------------------------------------------
void handleTouch() {
  if (!touchReady) return;

  // Poll touched() FIRST: it triggers a fresh XPT2046 read.
  // Calling getPoint() first returns stale coordinates from the previous touch.
  bool isTouched = touch.touched();
  TS_Point p = touch.getPoint();

  if (!wasTouched && isTouched) {
    if (touchUpAt != 0 && millis() - touchUpAt < TOUCH_DEBOUNCE_MS) {
      return;  // Bounce right after release: ignore
    }
    // Contact just began: record sample count, don't trust coordinates yet.
    touchDownSamples = 0;
    touchMoved = false;
    wasTouched = true;
    return;
  }

  if (wasTouched && isTouched) {
    touchDownSamples++;
    if (touchDownSamples < TOUCH_SETTLE_SAMPLES) {
      return;  // Discard noisy early samples while pressure stabilizes
    }
    int x = mapTouchToScreenX(p.x);
    int y = mapTouchToScreenY(p.y);
    if (touchDownSamples == TOUCH_SETTLE_SAMPLES) {
      // First stable sample: anchor the gesture here
      touchStartX = x;
      touchStartY = y;
      touchLastX = x;
      touchLastY = y;
      Serial.printf("touch down: raw=%d,%d screen=%d,%d z=%d\n", p.x, p.y, x, y, p.z);
      return;
    }
    if (abs(x - touchStartX) > 5 || abs(y - touchStartY) > 5) touchMoved = true;

    // Spike rejection: a single wild sample (shared-SPI glitch or pressure
    // flicker) would draw a long stray line. Skip jumps that are physically
    // impossible between two consecutive ~3ms samples.
    if (abs(x - touchLastX) > 60 || abs(y - touchLastY) > 60) {
      Serial.printf("touch spike rejected: %d,%d -> %d,%d\n", touchLastX, touchLastY, x, y);
      return;
    }

    // Light smoothing: average with previous point to reduce jitter
    int sx = (x + touchLastX) / 2;
    int sy = (y + touchLastY) / 2;

    if (drawModeActive && !isInAnyControl(sx, sy)) {
      drawLine(touchLastX, touchLastY, sx, sy);
      // First stroke of the session: reveal CLR/SEND buttons now
      if (overlayHasStrokes && !strokeButtonsShown) {
        strokeButtonsShown = true;
        renderUI();
      }
    }
    touchLastX = sx;
    touchLastY = sy;
    return;
  }

  if (wasTouched && !isTouched) {
    wasTouched = false;
    touchUpAt = millis();
    Serial.printf("touch up: screen=%d,%d moved=%s\n", touchStartX, touchStartY, touchMoved ? "yes" : "no");
    if (!touchMoved && touchDownSamples >= TOUCH_SETTLE_SAMPLES) {
      handleTap(touchStartX, touchStartY);
    }
  }
}

void handleTap(int x, int y) {
  if (isInButton(x, y, heartBtn)) {
    Serial.println("tap: heart");
    flashRect(heartBtn);
    if (sendLikeFeedback()) {
      showToast("Liked!");
    } else {
      showToast("Like failed");
    }
    return;
  }

  if (toolbarVisible) {
    // Color swatch selection
    for (int i = 0; i < SWATCH_COUNT; i++) {
      const ColorSwatch& s = swatches[i];
      if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) {
        activeColorIndex = i;
        Serial.printf("tap: color %d\n", i);
        renderUI();
        return;
      }
    }
    if (overlayHasStrokes && isInButton(x, y, clearBtn)) {
      Serial.println("tap: clear");
      flashRect(clearBtn);
      clearOverlay();
      renderScreen();
    } else if (overlayHasStrokes && isInButton(x, y, sendBtn)) {
      Serial.println("tap: send");
      flashRect(sendBtn);
      bool ok = sendDrawingFeedback();
      resetFeedbackState();
      renderScreen();
      showToast(ok ? "Sent!" : "Send failed");
    } else if (isInButton(x, y, closeBtn)) {
      Serial.println("tap: close");
      flashRect(closeBtn);
      drawModeActive = false;
      toolbarVisible = false;
      renderScreen();
    }
  } else {
    if (isInButton(x, y, penBtn)) {
      Serial.println("tap: pen");
      flashRect(penBtn);
      // Pen opens drawing mode directly: color bar + immediate drawing
      toolbarVisible = true;
      drawModeActive = true;
      strokeButtonsShown = false;
      renderScreen();
    }
  }
}

// ---------------------------------------------------------------------------
// Network feedback
// ---------------------------------------------------------------------------
bool sendLikeFeedback() {
  if (lastProcessedId.length() == 0) {
    Serial.println("like feedback: no message id");
    return false;
  }

  String url = String(API_HOST) + "/.netlify/functions/lovebox-feedback?deviceId=" + DEVICE_ID;
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Feedback-Type", "like");
  http.setTimeout(HTTP_TIMEOUT_MS);

  String payload = "{\"type\":\"like\",\"messageId\":\"" + lastProcessedId + "\"}";
  int httpCode = http.POST(payload);
  http.end();

  Serial.printf("like feedback HTTP %d, id=%s\\n", httpCode, lastProcessedId.c_str());
  return httpCode == 200;
}

bool sendDrawingFeedback() {
  if (lastProcessedId.length() == 0 || !imageStorageReady || !FFat.exists(IMAGE_PATH)) {
    Serial.printf("draw feedback: not ready, id=%s, storage=%s, image=%s\n",
      lastProcessedId.c_str(),
      imageStorageReady ? "yes" : "no",
      FFat.exists(IMAGE_PATH) ? "yes" : "no");
    return false;
  }

  File imageFile = FFat.open(IMAGE_PATH, FILE_READ);
  if (!imageFile || imageFile.size() != IMAGE_SIZE) {
    if (imageFile) imageFile.close();
    return false;
  }

  uint8_t* composed = new uint8_t[IMAGE_SIZE];
  if (!composed) {
    imageFile.close();
    return false;
  }

  if (imageFile.read(composed, IMAGE_SIZE) != IMAGE_SIZE) {
    delete[] composed;
    imageFile.close();
    return false;
  }
  imageFile.close();

  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      if (getOverlayPixel(x, y)) {
        int idx = (y * SCREEN_WIDTH + x) * 2;
        composed[idx] = 0xFF;
        composed[idx + 1] = 0xFF;
      }
    }
  }

  String url = String(API_HOST) + "/.netlify/functions/lovebox-feedback?deviceId=" + DEVICE_ID;
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Feedback-Type", "draw");
  http.addHeader("X-Message-Id", lastProcessedId);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int httpCode = http.sendRequest("POST", composed, IMAGE_SIZE);
  http.end();

  delete[] composed;
  Serial.printf("draw feedback HTTP %d, id=%s\n", httpCode, lastProcessedId.c_str());
  return httpCode == 200;
}

bool sendHealthReport() {
  if (WiFi.status() != WL_CONNECTED) return false;

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["uptimeMs"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["psramTotal"] = ESP.getPsramSize();
  doc["psramFree"] = ESP.getFreePsram();
  doc["ffatMounted"] = imageStorageReady;
  doc["ffatTotal"] = imageStorageReady ? FFat.totalBytes() : 0;
  doc["ffatUsed"] = imageStorageReady ? FFat.usedBytes() : 0;
  doc["resetReason"] = static_cast<int>(esp_reset_reason());
  doc["lastSuccessfulCommunicationMs"] = lastSuccessfulCommunicationAt;
  doc["lastMessageId"] = lastProcessedId;
  doc["displayReady"] = displayReady;
  doc["touchReady"] = touchReady;
  doc["audioReady"] = false;
  doc["servoReady"] = servoReady;

  String payload;
  serializeJson(doc, payload);

  String url = String(API_HOST) + "/.netlify/functions/lovebox-health?deviceId=" + DEVICE_ID;
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);
  int httpCode = http.POST(payload);
  http.end();

  Serial.printf("health HTTP %d, firmware=%s\n", httpCode, FIRMWARE_VERSION);
  return httpCode == 200;
}

int compareFirmwareVersions(const String& left, const String& right) {
  int leftMajor = 0, leftMinor = 0, leftPatch = 0;
  int rightMajor = 0, rightMinor = 0, rightPatch = 0;
  if (sscanf(left.c_str(), "%d.%d.%d", &leftMajor, &leftMinor, &leftPatch) != 3 ||
      sscanf(right.c_str(), "%d.%d.%d", &rightMajor, &rightMinor, &rightPatch) != 3) {
    return 0;
  }
  if (leftMajor != rightMajor) return leftMajor > rightMajor ? 1 : -1;
  if (leftMinor != rightMinor) return leftMinor > rightMinor ? 1 : -1;
  if (leftPatch != rightPatch) return leftPatch > rightPatch ? 1 : -1;
  return 0;
}

bool isSha256Hex(const String& value) {
  if (value.length() != 64) return false;
  for (size_t index = 0; index < value.length(); index++) {
    if (!isxdigit(value[index])) return false;
  }
  return true;
}

bool checkForFirmwareUpdate() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String manifestUrl = String(API_HOST) + "/.netlify/functions/lovebox-firmware?deviceId=" + DEVICE_ID;
  http.begin(secureClient, manifestUrl);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("firmware manifest HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument manifest;
  if (deserializeJson(manifest, payload) || !manifest["ok"].as<bool>()) {
    Serial.println("firmware manifest invalid");
    return false;
  }

  String version = manifest["data"]["version"].as<String>();
  String downloadUrl = manifest["data"]["url"].as<String>();
  String expectedSha256 = manifest["data"]["sha256"].as<String>();
  if (compareFirmwareVersions(version, FIRMWARE_VERSION) <= 0 ||
      !downloadUrl.startsWith("https://") || !isSha256Hex(expectedSha256)) {
    Serial.printf("no valid firmware update: version=%s\n", version.c_str());
    return false;
  }

  Serial.printf("firmware update available: %s -> %s\n", FIRMWARE_VERSION, version.c_str());
  http.begin(secureClient, downloadUrl);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(DOWNLOAD_TIMEOUT_MS);
  http.useHTTP10(false);
  httpCode = http.GET();
  int contentLength = http.getSize();
  if (httpCode != 200 || contentLength <= 0 || static_cast<size_t>(contentLength) > OTA_MAX_IMAGE_SIZE) {
    Serial.printf("firmware download rejected: HTTP %d, size=%d\n", httpCode, contentLength);
    http.end();
    return false;
  }

  if (!Update.begin(contentLength)) {
    Serial.printf("OTA begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  mbedtls_sha256_context sha256;
  mbedtls_sha256_init(&sha256);
  mbedtls_sha256_starts(&sha256, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t received = 0;
  bool writeOk = true;
  while (received < static_cast<size_t>(contentLength)) {
    size_t available = stream->available();
    if (available == 0) {
      delay(1);
      continue;
    }
    size_t requested = min(available, sizeof(buffer));
    int bytesRead = stream->readBytes(buffer, requested);
    if (bytesRead <= 0 || Update.write(buffer, bytesRead) != static_cast<size_t>(bytesRead)) {
      writeOk = false;
      break;
    }
    mbedtls_sha256_update(&sha256, buffer, bytesRead);
    received += bytesRead;
  }

  unsigned char digest[32];
  mbedtls_sha256_finish(&sha256, digest);
  mbedtls_sha256_free(&sha256);
  http.end();

  String actualSha256;
  char hex[3];
  for (size_t index = 0; index < sizeof(digest); index++) {
    snprintf(hex, sizeof(hex), "%02x", digest[index]);
    actualSha256 += hex;
  }

  if (!writeOk || received != static_cast<size_t>(contentLength) || actualSha256 != expectedSha256) {
    Serial.printf("OTA verification failed: received=%u/%d\n", received, contentLength);
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    Serial.printf("OTA finalize failed: %s\n", Update.errorString());
    return false;
  }

  Serial.println("OTA installed; rebooting");
  delay(250);
  ESP.restart();
  return true;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.setDebugOutput(true);
  Serial.println("Lovebox boot");
  esp_ota_mark_app_valid_cancel_rollback();

  // TFT setup
  tft.begin();
  displayReady = true;
  tft.setRotation(1);
  Serial.println("TFT initialized");
  tft.fillScreen(ILI9341_BLACK);

  // Touch setup (shared SPI bus with TFT, separate CS).
  // Order matters: configure custom pins first, then touch.begin() attaches
  // its interrupt; the library's internal SPI.begin() keeps existing pin mux.
  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchReady = touch.begin();
  Serial.printf("Touch ready: %s\n", touchReady ? "yes" : "no");

  // Servo setup
  heartServo.setPeriodHertz(50);
  servoReady = heartServo.attach(SERVO_PIN, 500, 2400) > 0;
  heartServo.write(SERVO_BASE_ANGLE);

  showScreen(
    "LOVEBOX",
    "Starting...",
    "Checking Wi-Fi"
  );

  WiFi.mode(WIFI_STA);

  WiFiManager wifiManager;

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Stop waiting forever if setup is abandoned
  wifiManager.setConfigPortalTimeout(180);

  // Useful during development only
  if (ERASE_SAVED_WIFI) {
    wifiManager.resetSettings();

    showScreen(
      "WI-FI RESET",
      "Saved network removed",
      "Restart device"
    );

    delay(3000);
    ESP.restart();
  }

  /*
    Behaviour:
    1. Tries previously saved Wi-Fi.
    2. If none exists or connection fails, creates LOVEBOX-SETUP.
    3. User connects by phone and enters Wi-Fi details.
    4. Credentials are stored automatically.
  */
  bool connected = wifiManager.autoConnect(
    "LOVEBOX-SETUP",
    "lovebox123"
  );

  if (!connected) {
    Serial.println("Wi-Fi setup failed or timed out");

    showScreen(
      "NO WI-FI",
      "Setup timed out",
      "Restart to retry",
      ILI9341_RED
    );

    delay(5000);
    ESP.restart();
  }

  Serial.println("Wi-Fi connected");
  Serial.print("Network: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  showScreen(
    "CONNECTED",
    WiFi.SSID(),
    WiFi.localIP().toString(),
    ILI9341_GREEN
  );

  showScreen("STORAGE", "Opening flash...", "");
  prefs.begin("lovebox", false);
  lastProcessedId = prefs.getString("lastId", "");
  currentCaption = prefs.getString("lastCaption", "");

  showScreen("STORAGE", "Preparing files...", "");
  imageStorageReady = FFat.begin(true);
  Serial.printf("FFat ready: %s\n", imageStorageReady ? "yes" : "no");
  Serial.printf("Cached image: %s\n", FFat.exists(IMAGE_PATH) ? "yes" : "no");

  // TODO: For production, replace setInsecure with a proper root CA certificate.
  secureClient.setInsecure();

  delay(2000);
  if (FFat.exists(IMAGE_PATH)) {
    Serial.println("Restoring cached image");
    showScreen("STORAGE", "Restoring image...", "");
  }
  if (!displayStoredImage()) {
    // No cached image (fresh FFat format, corruption, or wipe).
    // Clear the stored message ID so the current backend message is
    // re-downloaded instead of being skipped as "already displayed".
    if (lastProcessedId.length() > 0) {
      Serial.println("Cached image missing; clearing stored message ID");
      lastProcessedId = "";
      prefs.putString("lastId", "");
    }
    Serial.println("No cached image displayed");
    showScreen("WAITING", "Waiting for love...", "", ILI9341_PINK);
  } else {
    Serial.println("Cached image displayed");
    resetFeedbackState();
    displayCaption();
    renderUI();
  }

  lastHealthAt = millis();
  sendHealthReport();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  handleTouch();
  clearToast();

  if (WiFi.status() != WL_CONNECTED) {
    showScreen("NO WI-FI", "Wi-Fi lost", "Reconnecting...", ILI9341_RED);
    WiFi.reconnect();
    delay(5000);
    return;
  }

  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = millis();

    LatestMessage msg = fetchLatestMessage();

    if (msg.valid && msg.id != lastProcessedId) {
      if (downloadAndDisplayImage(msg.imageId)) {
        animateHeart();
        lastProcessedId = msg.id;
        currentCaption = msg.caption;
        prefs.putString("lastId", lastProcessedId);
        prefs.putString("lastCaption", currentCaption);
        resetFeedbackState();
        displayCaption();
        renderUI();
        sendAck();
      } else {
        showScreen("OOPS", "Image failed", lastImageError, ILI9341_RED);
        delay(2000);
        renderScreen();
      }
    }
  }

  if (millis() - lastHealthAt >= HEALTH_INTERVAL_MS) {
    lastHealthAt = millis();
    sendHealthReport();
  }

  if (millis() - lastOtaCheckAt >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheckAt = millis();
    checkForFirmwareUpdate();
  }

  delay(10);
}

// ---------------------------------------------------------------------------
// Network: fetch latest metadata
// ---------------------------------------------------------------------------
LatestMessage fetchLatestMessage() {
  LatestMessage msg = { "", "", "", "", false };

  String url = String(API_HOST) + "/.netlify/functions/lovebox-latest?deviceId=" + DEVICE_ID;

  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("latest HTTP %d\n", httpCode);
    http.end();
    return msg;
  }

  lastSuccessfulCommunicationAt = millis();

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("JSON parse failed");
    return msg;
  }

  if (!doc["ok"].as<bool>()) {
    Serial.println("API returned error");
    return msg;
  }

  JsonObject data = doc["data"];
  if (data.isNull()) {
    return msg; // No image yet
  }

  msg.id = data["id"].as<String>();
  msg.imageId = data["imageId"].as<String>();
  msg.caption = data["caption"].as<String>();
  msg.senderName = data["senderName"].as<String>();
  msg.valid = msg.id.length() > 0 && msg.imageId.length() > 0;
  currentCaption = msg.caption;

  return msg;
}

// ---------------------------------------------------------------------------
// Network: download and display image
// ---------------------------------------------------------------------------
bool downloadAndDisplayImage(const String& imageId) {
  lastImageError = "";
  String url = String(API_HOST) + "/.netlify/functions/lovebox-image?deviceId=" + DEVICE_ID + "&imageId=" + imageId;

  if (!imageStorageReady) {
    lastImageError = "Image storage unavailable";
    return false;
  }

  http.useHTTP10(false);
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(DOWNLOAD_TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    lastImageError = "HTTP " + String(httpCode);
    Serial.printf("image HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  showScreen("LOVE", "Downloading...", "Please wait", ILI9341_PINK);

  if (FFat.exists("/incoming.rgb565")) FFat.remove("/incoming.rgb565");
  File imageFile = FFat.open("/incoming.rgb565", FILE_WRITE);
  if (!imageFile) {
    lastImageError = "Cannot save image";
    http.end();
    return false;
  }

  int written = http.writeToStream(&imageFile);
  imageFile.close();
  http.end();

  if (written != IMAGE_SIZE) {
    lastImageError = "Received " + String(written);
    return false;
  }

  if (FFat.exists(IMAGE_PATH)) FFat.remove(IMAGE_PATH);
  if (!FFat.rename("/incoming.rgb565", IMAGE_PATH)) {
    lastImageError = "Cannot store image";
    return false;
  }

  showScreen("LOVE", "Displaying...", "", ILI9341_PINK);
  if (!displayStoredImage()) {
    lastImageError = "Cannot read image";
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Network: acknowledge delivery
// ---------------------------------------------------------------------------
void sendAck() {
  String url = String(API_HOST) + "/.netlify/functions/lovebox-ack?deviceId=" + DEVICE_ID;
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.POST("");
  http.end();
}

// ---------------------------------------------------------------------------
// Servo animation
// ---------------------------------------------------------------------------
void animateHeart() {
  moveServo(SERVO_BASE_ANGLE, SERVO_RIGHT_ANGLE);
  moveServo(SERVO_RIGHT_ANGLE, SERVO_BASE_ANGLE);
  moveServo(SERVO_BASE_ANGLE, SERVO_LEFT_ANGLE);
  moveServo(SERVO_LEFT_ANGLE, SERVO_BASE_ANGLE);
}

void moveServo(int fromAngle, int toAngle) {
  int step = (toAngle > fromAngle) ? 1 : -1;
  for (int pos = fromAngle; pos != toAngle; pos += step) {
    heartServo.write(pos);
    delay(SERVO_STEP_DELAY_MS);
  }
  heartServo.write(toAngle);
}
