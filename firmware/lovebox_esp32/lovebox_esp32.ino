/************************************************************
  Lovebox ESP32-S3 Firmware

  - Connects via WiFiManager captive portal
  - Polls Netlify backend for new images
  - Downloads 320x240 RGB565 binary and renders it on ILI9341
  - Animates SG90 servo heart on new image
  - Stores last processed image ID in NVS (Preferences)

  Required libraries:
    - TFT_eSPI
    - WiFiManager
    - ArduinoJson
    - ESP32Servo
************************************************************/

#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// User configuration: change these to match your deployment
// ---------------------------------------------------------------------------
const char* DEVICE_ID = "lovebox-001";
const char* DEVICE_KEY = "YOUR_DEVICE_KEY_HERE"; // Set this before flashing
const char* API_HOST = "https://effervescent-scone-29511f.netlify.app";

const int POLL_INTERVAL_MS = 5000;
const int HTTP_TIMEOUT_MS = 20000;
const int DOWNLOAD_TIMEOUT_MS = 30000;

const int SERVO_PIN = 15;
const int SERVO_BASE_ANGLE = 90;
const int SERVO_LEFT_ANGLE = 45;
const int SERVO_RIGHT_ANGLE = 135;
const int SERVO_STEP_DELAY_MS = 20;

const int TFT_WIDTH = 320;
const int TFT_HEIGHT = 240;
const int IMAGE_SIZE = TFT_WIDTH * TFT_HEIGHT * 2; // RGB565, 2 bytes per pixel

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
Servo heartServo;
WiFiClientSecure secureClient;
HTTPClient http;
Preferences prefs;

String lastProcessedId;

struct LatestMessage {
  String id;
  String imageId;
  String caption;
  String senderName;
  bool valid;
};

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  tft.init();
  tft.setRotation(1); // Landscape: 320x240
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  heartServo.attach(SERVO_PIN);
  heartServo.write(SERVO_BASE_ANGLE);

  prefs.begin("lovebox", false);
  lastProcessedId = prefs.getString("lastId", "");

  showBootScreen();

  WiFiManager wm;
  wm.setTitle("Lovebox Setup");
  wm.setAPName("LOVEBOX-SETUP");
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("LOVEBOX-SETUP")) {
    showMessage("Portal timeout\nRestarting...");
    delay(3000);
    ESP.restart();
  }

  showConnectedScreen();

  // TODO: For production, replace setInsecure with a proper root CA certificate.
  secureClient.setInsecure();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showMessage("Wi-Fi lost\nReconnecting...");
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
      showIdleScreen();
    } else {
      showMessage("Image failed\nRetrying...");
      delay(2000);
      showIdleScreen();
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
  String url = String(API_HOST) + "/.netlify/functions/lovebox-image?deviceId=" + DEVICE_ID + "&imageId=" + imageId;

  http.begin(secureClient, url);
  http.addHeader("X-Device-Key", DEVICE_KEY);
  http.setTimeout(DOWNLOAD_TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("image HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  int size = http.getSize();
  if (size != IMAGE_SIZE) {
    Serial.printf("Unexpected image size: %d\n", size);
    http.end();
    return false;
  }

  showMessage("Receiving love...");

  WiFiClient* stream = http.getStreamPtr();

  tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
  tft.startWrite();

  const int CHUNK_BYTES = 1024;
  uint8_t byteBuffer[CHUNK_BYTES];
  uint16_t pixelBuffer[CHUNK_BYTES / 2];
  int remaining = size;
  int pending = 0;
  unsigned long lastData = millis();

  while (remaining > 0) {
    int available = stream->available();
    if (available > 0) {
      int toRead = min(available, CHUNK_BYTES - pending);
      int read = stream->readBytes(byteBuffer + pending, toRead);
      pending += read;
      remaining -= read;
      lastData = millis();

      int pixels = pending / 2;
      if (pixels > 0) {
        for (int i = 0; i < pixels; i++) {
          pixelBuffer[i] = byteBuffer[i * 2] | (byteBuffer[i * 2 + 1] << 8);
        }
        tft.pushPixels(pixelBuffer, pixels);

        int leftover = pending % 2;
        if (leftover > 0) {
          byteBuffer[0] = byteBuffer[pixels * 2];
        }
        pending = leftover;
      }
    } else if (millis() - lastData > DOWNLOAD_TIMEOUT_MS) {
      Serial.println("Download timeout");
      break;
    } else {
      delay(1);
    }
  }

  tft.endWrite();
  http.end();

  return remaining == 0;
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
// Servo animation: 90 -> 135 -> 90 -> 45 -> 90
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
// Screen helpers
// ---------------------------------------------------------------------------
void showBootScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Starting Lovebox", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 10);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("Please wait...", TFT_WIDTH / 2, TFT_HEIGHT / 2 + 20);
}

void showConnectedScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Connected", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 20);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString(WiFi.localIP().toString(), TFT_WIDTH / 2, TFT_HEIGHT / 2 + 10);
  delay(2000);
  showIdleScreen();
}

void showIdleScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.setTextSize(3);
  tft.drawString("\x03", TFT_WIDTH / 2, TFT_HEIGHT / 2 - 25); // Heart char
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("Waiting for love...", TFT_WIDTH / 2, TFT_HEIGHT / 2 + 25);
}

void showMessage(const char* text) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(text, TFT_WIDTH / 2, TFT_HEIGHT / 2);
}
