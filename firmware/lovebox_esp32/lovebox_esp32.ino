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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FFat.h>
#include <ESP32Servo.h>
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------
const char* DEVICE_ID = "lovebox-001";
const char* DEVICE_KEY = "YOUR_DEVICE_KEY_HERE"; // Set this before flashing
const char* API_HOST = "https://effervescent-scone-29511f.netlify.app";

// ---------------- TFT pins ----------------
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
const int RAW_X_MIN = 450;
const int RAW_X_MAX = 3905;
const int RAW_Y_MIN = 343;
const int RAW_Y_MAX = 3795;

// Set true only once when you deliberately want to erase saved Wi-Fi
#define ERASE_SAVED_WIFI false

const int POLL_INTERVAL_MS = 5000;
const int HTTP_TIMEOUT_MS = 20000;
const int DOWNLOAD_TIMEOUT_MS = 30000;

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
bool imageStorageReady = false;

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

const Button heartBtn = { 275, 5, 40, 30 };
const Button penBtn = { 5, 205, 30, 30 };

const int TOOLBAR_Y = 200;
const int TOOLBAR_H = 40;
const Button drawBtn = { 10, 205, 70, 30 };
const Button clearBtn = { 90, 205, 70, 30 };
const Button sendBtn = { 170, 205, 70, 30 };
const Button closeBtn = { 250, 205, 60, 30 };

// 1-bit overlay: 320 * 240 / 8 = 9600 bytes
uint8_t overlayBuffer[(SCREEN_WIDTH * SCREEN_HEIGHT) / 8];

bool toolbarVisible = false;
bool drawModeActive = false;

bool touchReady = false;
bool wasTouched = false;
int touchStartX = 0;
int touchStartY = 0;
int touchLastX = 0;
int touchLastY = 0;
bool touchMoved = false;
unsigned long lastPollAt = 0;

String toastText;
unsigned long toastUntil = 0;

// Forward declarations for touch event handlers
void handleTap(int x, int y);
bool sendLikeFeedback();
bool sendDrawingFeedback();

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
int mapTouchToScreenX(int rawY) {
  return constrain(map(rawY, RAW_Y_MAX, RAW_Y_MIN, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
}

int mapTouchToScreenY(int rawX) {
  return constrain(map(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
}

bool isInButton(int x, int y, const Button& btn) {
  return x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h;
}

bool isInAnyControl(int x, int y) {
  if (isInButton(x, y, heartBtn)) return true;
  if (!toolbarVisible) {
    if (isInButton(x, y, penBtn)) return true;
  } else {
    if (isInButton(x, y, drawBtn)) return true;
    if (isInButton(x, y, clearBtn)) return true;
    if (isInButton(x, y, sendBtn)) return true;
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
  tft.drawPixel(x, y, TFT_WHITE);
}

void clearOverlay() {
  memset(overlayBuffer, 0, sizeof(overlayBuffer));
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
}

// ---------------------------------------------------------------------------
// UI rendering
// ---------------------------------------------------------------------------
void drawButton(const Button& btn, uint16_t color, const char* label) {
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, color);
  tft.setTextColor(ILI9341_WHITE, color);
  tft.setTextSize(1);
  int16_t textWidth = strlen(label) * 6;
  tft.setCursor(btn.x + (btn.w - textWidth) / 2, btn.y + (btn.h - 8) / 2);
  tft.print(label);
}

void renderUI() {
  drawButton(heartBtn, ILI9341_RED, "<3");

  if (!toolbarVisible) {
    drawButton(penBtn, ILI9341_BLUE, "PEN");
  } else {
    tft.fillRect(0, TOOLBAR_Y, SCREEN_WIDTH, TOOLBAR_H, ILI9341_BLACK);
    drawButton(drawBtn, drawModeActive ? ILI9341_GREEN : ILI9341_BLUE, "DRAW");
    drawButton(clearBtn, ILI9341_YELLOW, "CLR");
    drawButton(sendBtn, ILI9341_PINK, "SEND");
    drawButton(closeBtn, ILI9341_RED, "X");
  }
}

void renderScreen() {
  displayStoredImage();
  renderUI();
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

  TS_Point p = touch.getPoint();
  bool isTouched = touch.touched() && p.z > 200;

  if (!wasTouched && isTouched) {
    touchStartX = mapTouchToScreenX(p.y);
    touchStartY = mapTouchToScreenY(p.x);
    touchLastX = touchStartX;
    touchLastY = touchStartY;
    touchMoved = false;
  } else if (wasTouched && isTouched) {
    int x = mapTouchToScreenX(p.y);
    int y = mapTouchToScreenY(p.x);
    if (abs(x - touchStartX) > 5 || abs(y - touchStartY) > 5) touchMoved = true;

    if (drawModeActive && !isInAnyControl(x, y)) {
      drawLine(touchLastX, touchLastY, x, y);
    }
    touchLastX = x;
    touchLastY = y;
  } else if (wasTouched && !isTouched) {
    if (!touchMoved) {
      handleTap(touchStartX, touchStartY);
    }
  }
  wasTouched = isTouched;
}

void handleTap(int x, int y) {
  if (isInButton(x, y, heartBtn)) {
    if (sendLikeFeedback()) {
      showToast("Liked!");
    } else {
      showToast("Like failed");
    }
    return;
  }

  if (toolbarVisible) {
    if (isInButton(x, y, drawBtn)) {
      drawModeActive = !drawModeActive;
      renderUI();
    } else if (isInButton(x, y, clearBtn)) {
      clearOverlay();
      renderScreen();
    } else if (isInButton(x, y, sendBtn)) {
      bool ok = sendDrawingFeedback();
      resetFeedbackState();
      renderScreen();
      showToast(ok ? "Sent!" : "Send failed");
    } else if (isInButton(x, y, closeBtn)) {
      drawModeActive = false;
      toolbarVisible = false;
      renderScreen();
    }
  } else {
    if (isInButton(x, y, penBtn)) {
      toolbarVisible = true;
      renderUI();
    }
  }
}

// ---------------------------------------------------------------------------
// Network feedback
// ---------------------------------------------------------------------------
bool sendLikeFeedback() {
  if (lastProcessedId.length() == 0) return false;

  String url = String(API_HOST) + "/.netlify/functions/lovebox-feedback?deviceId=" + DEVICE_ID;
  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);

  String payload = "{\"type\":\"like\",\"messageId\":\"" + lastProcessedId + "\"}";
  int httpCode = http.POST(payload);
  http.end();

  Serial.printf("like feedback HTTP %d\\n", httpCode);
  return httpCode == 200;
}

bool sendDrawingFeedback() {
  if (lastProcessedId.length() == 0 || !imageStorageReady || !FFat.exists(IMAGE_PATH)) {
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
  Serial.printf("draw feedback HTTP %d\\n", httpCode);
  return httpCode == 200;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.setDebugOutput(true);
  Serial.println("Lovebox boot");

  // TFT setup
  tft.begin();
  tft.setRotation(1);
  Serial.println("TFT initialized");
  tft.fillScreen(ILI9341_BLACK);

  // Touch setup (shared SPI pins with TFT, separate CS)
  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchReady = touch.begin(SPI);
  Serial.printf("Touch ready: %s\n", touchReady ? "yes" : "no");

  // Servo setup
  heartServo.setPeriodHertz(50);
  heartServo.attach(SERVO_PIN, 500, 2400);
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
    Serial.println("No cached image displayed");
    showScreen("WAITING", "Waiting for love...", "", ILI9341_PINK);
  } else {
    Serial.println("Cached image displayed");
    resetFeedbackState();
    renderUI();
  }
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
        prefs.putString("lastId", lastProcessedId);
        resetFeedbackState();
        renderUI();
        sendAck();
      } else {
        showScreen("OOPS", "Image failed", lastImageError, ILI9341_RED);
        delay(2000);
        renderScreen();
      }
    }
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
