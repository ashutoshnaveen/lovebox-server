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

void setup() {
  Serial.begin(115200);
  delay(500);

  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touchReady = touch.begin(SPI);
  SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
}

void loop() {
  if (millis() - lastStatusAt >= 2000) {
    Serial.println(touchReady ? "Touch calibration ready - tap the screen" : "Touch controller not detected");
    lastStatusAt = millis();
  }

  if (!touchReady || millis() - lastSampleAt < 250) return;

  TS_Point point = touch.getPoint();
  Serial.printf("raw x=%d y=%d pressure=%d\n", point.x, point.y, point.z);
  lastSampleAt = millis();
}
