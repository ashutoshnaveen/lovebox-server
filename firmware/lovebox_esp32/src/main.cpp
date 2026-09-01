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
#include <driver/i2s.h>
#include <string.h>

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

// ---------------- I2S Audio pins (MAX98357A) ----------------
#define I2S_LRC_PIN   4   // LRCLK / WS
#define I2S_BCLK_PIN  5   // BCLK / SCK
#define I2S_DIN_PIN   6   // DIN / SD
// GAIN and SD pins left unconnected (default gain, always enabled)

// Audio playback gain. 1.0 = no boost, 1.6 = moderate boost.
// Lower this if audio distorts at peak volumes.
const float AUDIO_DIGITAL_GAIN = 1.0f;

const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;
const int IMAGE_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT * 2; // RGB565, 2 bytes per pixel
const char* IMAGE_PATH = "/latest.rgb565";
const char* AUDIO_PATH = "/audio.wav";

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
String currentMessageCaption;
bool imageStorageReady = false;
bool displayReady = false;
bool servoReady = false;
bool audioReady = false;
unsigned long lastHealthAt = 0;
unsigned long lastSuccessfulCommunicationAt = 0;
unsigned long lastOtaCheckAt = 0;

struct LatestMessage {
  String id;
  String imageId;
  String audioId;
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

const Button heartBtn  = { 278, 210, 42, 30 };
const Button capBtn    = { 0, 0, 88, 30 };
const Button penBtn    = { 0, 210, 45, 30 };
const Button replayBtn = { 265, 0, 55, 30 };

const int TOOLBAR_Y = 195;
const int TOOLBAR_H = 45;
const Button clearBtn = { 0, 200, 45, 30 };
const Button sendBtn = { 55, 200, 55, 30 };
const Button closeBtn = { 260, 200, 60, 30 };

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

// Color-index overlay: 320 * 240 = 76800 bytes
// 0 = empty, 1..SWATCH_COUNT = color index + 1
uint8_t overlayBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

bool captionVisible = true;
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
bool downloadAudioFile(const String& audioId);
bool playAudioFile();
bool playNotificationTone();
void renderScreen();
void handleTap(int x, int y);
bool sendLikeFeedback();
bool sendDrawingFeedback();
bool sendHealthReport();
bool checkForFirmwareUpdate();
bool initI2SAudio();

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
  if (currentMessageCaption.length() > 0 && isInButton(x, y, capBtn)) return true;
  if (!toolbarVisible) {
    if (isInButton(x, y, penBtn)) return true;
    if (isInButton(x, y, replayBtn)) return true;
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

uint8_t getOverlayPixel(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return 0;
  int idx = y * SCREEN_WIDTH + x;
  return overlayBuffer[idx];
}

void setOverlayPixel(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  int idx = y * SCREEN_WIDTH + x;
  if (overlayBuffer[idx] != 0) return;
  overlayBuffer[idx] = activeColorIndex + 1;
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
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 3, bg);
  tft.setTextColor(fg, bg);
  tft.setTextSize(2);
  int16_t w = strlen(label) * 12;
  tft.setCursor(btn.x + (btn.w - w) / 2, btn.y + 8);
  tft.print(label);
}

void drawHeartButton(const Button& btn) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 3, ILI9341_RED);
  int cx = btn.x + btn.w / 2;
  int cy = btn.y + btn.h / 2;
  tft.fillCircle(cx - 7, cy - 3, 6, ILI9341_WHITE);
  tft.fillCircle(cx + 7, cy - 3, 6, ILI9341_WHITE);
  tft.fillTriangle(cx - 13, cy - 1, cx + 13, cy - 1, cx, cy + 12, ILI9341_WHITE);
}

void renderUI() {
  if (!toolbarVisible) {
    drawHeartButton(heartBtn);
  }

  if (currentMessageCaption.length() > 0) {
    drawButton(capBtn, captionVisible ? 0x780F : 0x5A69, ILI9341_WHITE, "CAPTION");
  }

  if (!toolbarVisible) {
    // Blue background -> white text
    drawButton(penBtn, ILI9341_BLUE, ILI9341_WHITE, "PEN");
    // Purple/Magenta background -> white text
    drawButton(replayBtn, 0x780F, ILI9341_WHITE, "PLAY");
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
  if (!displayStoredImage()) {
    tft.fillScreen(ILI9341_BLACK);
  }
  displayCaption();
  renderUI();
}

void flashRect(const Button& btn) {
  tft.drawRect(btn.x - 2, btn.y - 2, btn.w + 4, btn.h + 4, ILI9341_WHITE);
  delay(120);
  renderUI();
}

void displayCaption() {
  if (!captionVisible || currentCaption.length() == 0) return;
  const int CHAR_W = 6;
  const int LINE_H = 8;
  const int MARGIN_X = 4;
  const int MARGIN_Y = 3;
  const int maxW = SCREEN_WIDTH - MARGIN_X * 2;
  int lines = 1;
  int w = 0;
  for (int i = 0; i < currentCaption.length(); i++) {
    if (currentCaption[i] == '\n') {
      lines++;
      w = 0;
    } else {
      w += CHAR_W;
      if (w > maxW) {
        lines++;
        w = CHAR_W;
      }
    }
  }
  const int capY = 42;
  const int capH = MARGIN_Y * 2 + lines * LINE_H;
  tft.fillRect(0, capY, SCREEN_WIDTH, capH, ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setCursor(MARGIN_X, capY + MARGIN_Y);
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
  if (!touchReady) {
    static unsigned long lastTouchReadyLog = 0;
    if (millis() - lastTouchReadyLog > 5000) {
      Serial.println("touch skipped: not ready");
      lastTouchReadyLog = millis();
    }
    return;
  }

  bool isTouched = touch.touched();
  TS_Point p = touch.getPoint();

  if (isTouched) {
    Serial.printf("touch raw: x=%d y=%d z=%d\n", p.x, p.y, p.z);
  }

  if (!wasTouched && isTouched) {
    if (touchUpAt != 0 && millis() - touchUpAt < TOUCH_DEBOUNCE_MS) {
      Serial.println("touch debounce: ignore bounce");
      return;
    }
    touchDownSamples = 0;
    touchMoved = false;
    wasTouched = true;
    Serial.println("touch state: down");
    return;
  }

  if (wasTouched && isTouched) {
    touchDownSamples++;
    if (touchDownSamples < TOUCH_SETTLE_SAMPLES) {
      Serial.printf("touch settling: sample %d/%d\n", touchDownSamples, TOUCH_SETTLE_SAMPLES);
      return;
    }
    int x = mapTouchToScreenX(p.x);
    int y = mapTouchToScreenY(p.y);
    if (touchDownSamples == TOUCH_SETTLE_SAMPLES) {
      touchStartX = x;
      touchStartY = y;
      touchLastX = x;
      touchLastY = y;
      Serial.printf("touch anchor: raw=%d,%d screen=%d,%d z=%d\n", p.x, p.y, x, y, p.z);
      return;
    }
    if (abs(x - touchStartX) > 10 || abs(y - touchStartY) > 10) touchMoved = true;

    if (abs(x - touchLastX) > 60 || abs(y - touchLastY) > 60) {
      Serial.printf("touch spike rejected: %d,%d -> %d,%d\n", touchLastX, touchLastY, x, y);
      return;
    }

    int sx = (x + touchLastX) / 2;
    int sy = (y + touchLastY) / 2;

    if (drawModeActive && !isInAnyControl(sx, sy)) {
      drawLine(touchLastX, touchLastY, sx, sy);
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
    Serial.printf("touch up: screen=%d,%d moved=%s samples=%d\n", touchStartX, touchStartY, touchMoved ? "yes" : "no", touchDownSamples);
    if (!touchMoved && touchDownSamples >= TOUCH_SETTLE_SAMPLES) {
      handleTap(touchStartX, touchStartY);
    }
  }
}

void handleTap(int x, int y) {
  if (!toolbarVisible && isInButton(x, y, heartBtn)) {
    Serial.println("tap: heart");
    flashRect(heartBtn);
    if (sendLikeFeedback()) {
      showToast("Liked!");
    } else {
      showToast("Like failed");
    }
    return;
  }

  if (currentMessageCaption.length() > 0 && isInButton(x, y, capBtn)) {
    Serial.println("tap: caption toggle");
    flashRect(capBtn);
    captionVisible = !captionVisible;
    renderScreen();
    showToast(captionVisible ? "Caption ON" : "Caption OFF");
    return;
  }

  if (toolbarVisible) {
    // Color swatch selection
    for (int i = 0; i < SWATCH_COUNT; i++) {
      const ColorSwatch& s = swatches[i];
      if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) {
        activeColorIndex = i;
        Serial.printf("tap: color %d rgb565=0x%04X\n", i, swatches[i].color);
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
      return;
    }
    if (isInButton(x, y, replayBtn)) {
      Serial.println("tap: replay");
      flashRect(replayBtn);
      showToast("Playing...");
      if (FFat.exists(AUDIO_PATH)) {
        playAudioFile();
      } else {
        playNotificationTone();
      }
      toastUntil = 0;
      renderScreen();
      return;
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

  uint16_t drawColor = swatches[activeColorIndex].color;
  int colorCounts[SWATCH_COUNT] = {0};
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      uint8_t colorIndex = getOverlayPixel(x, y);
      if (colorIndex != 0) {
        int idx = (y * SCREEN_WIDTH + x) * 2;
        drawColor = swatches[colorIndex - 1].color;
        composed[idx] = drawColor & 0xFF;
        composed[idx + 1] = (drawColor >> 8) & 0xFF;
        if (colorIndex - 1 < SWATCH_COUNT) {
          colorCounts[colorIndex - 1]++;
        }
      }
    }
  }
  Serial.printf("draw feedback colors: white=%d red=%d yellow=%d green=%d blue=%d\n",
    colorCounts[0], colorCounts[1], colorCounts[2], colorCounts[3], colorCounts[4]);

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
  doc["audioReady"] = audioReady;
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
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  touchReady = touch.begin();
  Serial.printf("Touch ready: %s\n", touchReady ? "yes" : "no");

  // Servo setup
  heartServo.setPeriodHertz(50);
  servoReady = heartServo.attach(SERVO_PIN, 500, 2400) > 0;
  heartServo.write(SERVO_BASE_ANGLE);

  // I2S Audio setup (MAX98357A)
  audioReady = initI2SAudio();
  Serial.printf("Audio ready: %s\n", audioReady ? "yes" : "no");

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
  currentMessageCaption = currentCaption;

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
        toastUntil = 0;
        lastProcessedId = msg.id;
        currentCaption = msg.caption;
        currentMessageCaption = msg.caption;
        captionVisible = true;
        prefs.putString("lastId", lastProcessedId);
        prefs.putString("lastCaption", currentCaption);
        resetFeedbackState();
        renderScreen();
        sendAck();
        if (msg.audioId.length() > 0) {
          if (downloadAudioFile(msg.audioId)) {
            if (!playAudioFile()) {
              showToast("Audio play failed");
            }
          }
        } else {
          playNotificationTone();
        }
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

LatestMessage fetchLatestMessage() {
  LatestMessage msg = { "", "", "", "", "", false };

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
  msg.audioId = data["audioId"].as<String>();
  msg.caption = data["caption"].as<String>();
  msg.senderName = data["senderName"].as<String>();
  msg.valid = msg.id.length() > 0 && msg.imageId.length() > 0;

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
// Network: download and play voice-note audio (16-bit PCM WAV)
// ---------------------------------------------------------------------------
bool downloadAudioFile(const String& audioId) {
  if (!imageStorageReady || WiFi.status() != WL_CONNECTED) {
    Serial.println("audio download skipped: storage/wifi not ready");
    return false;
  }

  String url = String(API_HOST) + "/.netlify/functions/lovebox-audio?deviceId=" + DEVICE_ID + "&audioId=" + audioId;
  http.useHTTP10(false);
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(DOWNLOAD_TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("audio download failed HTTP %d for %s\n", httpCode, audioId.c_str());
    http.end();
    showToast("Audio download failed");
    return false;
  }

  showToast("Voice note...");

  if (FFat.exists(AUDIO_PATH)) FFat.remove(AUDIO_PATH);
  File audioFile = FFat.open(AUDIO_PATH, FILE_WRITE);
  if (!audioFile) {
    Serial.println("audio file open failed for write");
    http.end();
    showToast("Audio save failed");
    return false;
  }

  int written = http.writeToStream(&audioFile);
  audioFile.close();
  http.end();

  Serial.printf("audio downloaded: %d bytes for %s\n", written, audioId.c_str());
  if (written <= 44) {
    showToast("Audio file too short");
    return false;
  }
  return true;
}

bool playAudioFile() {
  File f = FFat.open(AUDIO_PATH, FILE_READ);
  if (!f) {
    Serial.println("audio play failed: cannot open /audio.wav");
    return false;
  }

  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12 || strncmp((char*)hdr, "RIFF", 4) != 0) {
    Serial.println("audio play failed: missing RIFF header");
    f.close();
    return false;
  }

  uint32_t sampleRate = 16000;
  uint16_t channels = 1;
  uint16_t bits = 16;
  bool foundData = false;
  uint32_t dataSize = 0;

  while (f.available()) {
    uint8_t chunkId[4];
    uint8_t chunkSizeBytes[4];
    if (f.read(chunkId, 4) != 4) break;
    if (f.read(chunkSizeBytes, 4) != 4) break;
    uint32_t chunkSize = chunkSizeBytes[0] | (chunkSizeBytes[1] << 8) |
                         ((uint32_t)chunkSizeBytes[2] << 16) | ((uint32_t)chunkSizeBytes[3] << 24);

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      uint8_t fmt[16];
      if (f.read(fmt, 16) == 16) {
        channels = fmt[2] | (fmt[3] << 8);
        sampleRate = fmt[4] | (fmt[5] << 8) | ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
        bits = fmt[14] | (fmt[15] << 8);
      }
      int32_t skip = (int32_t)chunkSize - 16;
      while (skip > 0) {
        uint8_t dump[32];
        int n = f.read(dump, skip > 32 ? 32 : skip);
        if (n <= 0) break;
        skip -= n;
      }
    } else if (memcmp(chunkId, "data", 4) == 0) {
      dataSize = chunkSize;
      foundData = true;
      break;
    } else {
      int32_t skip = (int32_t)chunkSize;
      while (skip > 0) {
        uint8_t dump[32];
        int n = f.read(dump, skip > 32 ? 32 : skip);
        if (n <= 0) break;
        skip -= n;
      }
    }
  }

  if (!foundData || dataSize == 0) {
    Serial.printf("audio play failed: data chunk missing or empty (found=%d size=%u)\n", foundData, dataSize);
    f.close();
    return false;
  }

  Serial.printf("audio playback start: %u Hz, %u ch, %u bit, %u bytes\n", sampleRate, channels, bits, dataSize);

  i2s_set_clk(I2S_NUM_0, sampleRate, (i2s_bits_per_sample_t)bits, I2S_CHANNEL_STEREO);

  const int CHUNK = 1024;
  uint8_t buf[CHUNK];
  static uint8_t stereoBuf[CHUNK * 2];
  uint32_t remaining = dataSize;
  bool mono = (channels == 1);
  size_t bytesWritten = 0;
  const float DIGITAL_GAIN = AUDIO_DIGITAL_GAIN;
  int32_t peakSample = 0;

  while (remaining > 0) {
    uint32_t toRead = remaining > CHUNK ? CHUNK : remaining;
    int rd = f.read(buf, toRead);
    if (rd <= 0) break;

    if (mono && bits == 16) {
      int samples = rd / 2;
      for (int i = 0; i < samples; i++) {
        int16_t raw = (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
        int32_t amplified = (int32_t)(raw * DIGITAL_GAIN);
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        int32_t absSample = amplified < 0 ? -amplified : amplified;
        if (absSample > peakSample) peakSample = absSample;
        uint16_t s = (uint16_t)(int16_t)amplified;
        stereoBuf[i * 4]     = s & 0xFF;
        stereoBuf[i * 4 + 1] = (s >> 8) & 0xFF;
        stereoBuf[i * 4 + 2] = s & 0xFF;
        stereoBuf[i * 4 + 3] = (s >> 8) & 0xFF;
      }
      i2s_write(I2S_NUM_0, stereoBuf, (size_t)samples * 4, &bytesWritten, portMAX_DELAY);
    } else {
      if (bits == 16) {
        int samples = rd / 2;
        for (int i = 0; i < samples; i++) {
          int16_t raw = (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
          int32_t amplified = (int32_t)(raw * DIGITAL_GAIN);
          if (amplified > 32767) amplified = 32767;
          if (amplified < -32768) amplified = -32768;
          int32_t absSample = amplified < 0 ? -amplified : amplified;
          if (absSample > peakSample) peakSample = absSample;
          uint16_t s = (uint16_t)(int16_t)amplified;
          buf[i * 2]     = s & 0xFF;
          buf[i * 2 + 1] = (s >> 8) & 0xFF;
        }
      }
      i2s_write(I2S_NUM_0, buf, (size_t)rd, &bytesWritten, portMAX_DELAY);
    }
    remaining -= rd;
    yield();
  }

  Serial.printf("audio playback peak level: %d / 32767 (%.1f%%)\n", peakSample, (peakSample * 100.0f) / 32767.0f);
  f.close();
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("audio playback finished");
  return true;
}

bool playNotificationTone() {
  const uint32_t sampleRate = 16000;
  i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

  // 4 notes (C5 523Hz, E5 659Hz, G5 784Hz, C6 1046Hz) with smooth decay
  const float frequencies[4] = { 523.25f, 659.25f, 783.99f, 1046.50f };
  const int totalSamples = sampleRate * 0.95f;

  const int CHUNK = 256;
  uint8_t stereoBuf[CHUNK * 4];
  size_t bytesWritten = 0;

  for (int sampleIdx = 0; sampleIdx < totalSamples; sampleIdx += CHUNK) {
    int count = min(CHUNK, totalSamples - sampleIdx);
    for (int i = 0; i < count; i++) {
      int curSample = sampleIdx + i;
      float val = 0.0f;
      for (int n = 0; n < 4; n++) {
        int noteStart = n * (sampleRate * 0.14f);
        if (curSample >= noteStart) {
          float t = (float)(curSample - noteStart) / (float)sampleRate;
          float env = expf(-4.5f * t);
          val += 0.45f * sinf(2.0f * 3.14159265f * frequencies[n] * t) * env;
        }
      }
      val = constrain(val, -0.98f, 0.98f);
      int16_t s = (int16_t)(val * 32000.0f);
      stereoBuf[i * 4]     = s & 0xFF;
      stereoBuf[i * 4 + 1] = (s >> 8) & 0xFF;
      stereoBuf[i * 4 + 2] = s & 0xFF;
      stereoBuf[i * 4 + 3] = (s >> 8) & 0xFF;
    }
    i2s_write(I2S_NUM_0, stereoBuf, (size_t)count * 4, &bytesWritten, portMAX_DELAY);
    yield();
  }
  Serial.println("notification tone finished");
  return true;
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

// ---------------------------------------------------------------------------
// I2S Audio initialization (MAX98357A)
// ---------------------------------------------------------------------------
bool initI2SAudio() {
  // Configure I2S for MAX98357A (I2S Philips standard, 16-bit, stereo)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %s\n", esp_err_to_name(err));
    return false;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S set pin failed: %s\n", esp_err_to_name(err));
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }

  Serial.println("I2S (MAX98357A) initialized on GPIO 4(LRC), 5(BCLK), 6(DIN)");
  return true;
}
