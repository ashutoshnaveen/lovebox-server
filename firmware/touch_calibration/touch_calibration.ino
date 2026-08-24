#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#define TOUCH_MOSI 11
#define TOUCH_SCK 12
#define TOUCH_MISO 13
#define TOUCH_CS 16
#define TOUCH_IRQ 17

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
bool touchReady = false;
unsigned long lastStatusAt = 0;
unsigned long lastSampleAt = 0;

// Same calibration bounds as the main firmware
const int RAW_X_MIN = 439;
const int RAW_X_MAX = 3867;
const int RAW_Y_MIN = 360;
const int RAW_Y_MAX = 3795;
const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

int mapToScreenX(int rawX) {
  return constrain(map(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
}

int mapToScreenY(int rawY) {
  return constrain(map(rawY, RAW_Y_MIN, RAW_Y_MAX, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchReady = touch.begin(SPI);
  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
}

void loop() {
  if (millis() - lastStatusAt >= 2000) {
    Serial.println(touchReady ? "Touch calibration ready - tap the four corners" : "Touch controller not detected");
    lastStatusAt = millis();
  }

  if (!touchReady || millis() - lastSampleAt < 250) return;

  TS_Point point = touch.getPoint();
  int sx = mapToScreenX(point.x);
  int sy = mapToScreenY(point.y);
  Serial.printf("raw x=%d y=%d pressure=%d -> screen x=%d y=%d\n", point.x, point.y, point.z, sx, sy);
  lastSampleAt = millis();
}
