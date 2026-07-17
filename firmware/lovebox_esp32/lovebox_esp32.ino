/************************************************************
  Lovebox ESP32-S3 Firmware

  - Connects via WiFiManager captive portal
  - Polls Netlify backend for new images
  - Downloads 320x240 RGB565 binary and renders it on ILI9341
  - Animates SG90 servo on new image
  - Stores last processed image ID in NVS (Preferences)

  Required libraries:
    - LovyanGFX
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
  }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showScreen("NO WI-FI", "Wi-Fi lost", "Reconnecting...", ILI9341_RED);
    WiFi.reconnect();
    delay(5000);
    return;
  }

  LatestMessage msg = fetchLatestMessage();

  if (msg.valid && msg.id != lastProcessedId) {
    if (downloadAndDisplayImage(msg.imageId)) {
      animateHeart();
      lastProcessedId = msg.id;
      prefs.putString("lastId", lastProcessedId);
      sendAck();
    } else {
      showScreen("OOPS", "Image failed", lastImageError, ILI9341_RED);
      delay(2000);
    }
  }

  delay(POLL_INTERVAL_MS);
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
