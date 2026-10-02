#include <Arduino.h>
#include <Adafruit_DotStar.h>
#include <SPI.h>

// -----------------------------------------------------------------------------
// Existing simple blink pattern on LED_BUILTIN
// -----------------------------------------------------------------------------

// Timings match your original code: 1000ms HIGH, 1000ms LOW,
// then three 100ms HIGH/LOW pulses.
const uint32_t blinkPattern[] = {1000, 1000, 100, 100, 100, 100, 100, 100};
const int patternLength = sizeof(blinkPattern) / sizeof(blinkPattern[0]);

int patternIndex = 0;
unsigned long previousMillis = 0;
bool ledState = HIGH;

// -----------------------------------------------------------------------------
// Built-in addressable RGB LED for blinkx
// -----------------------------------------------------------------------------
//
// Adafruit ItsyBitsy M0 Express has a built-in DotStar RGB LED.
// Most Adafruit SAMD board packages define these pin names. The fallbacks below
// match the common ItsyBitsy M0 Express DotStar wiring.
//

#ifndef PIN_DOTSTAR_DATA
#define PIN_DOTSTAR_DATA 41
#endif

#ifndef PIN_DOTSTAR_CLOCK
#define PIN_DOTSTAR_CLOCK 40
#endif

Adafruit_DotStar rgbLed(1, PIN_DOTSTAR_DATA, PIN_DOTSTAR_CLOCK, DOTSTAR_BGR);

const uint32_t BLINKX_BLUE = 0x0000FF;
const uint32_t BLINKX_OFF  = 0x000000;

int blinkxTogglesRemaining = 0;
bool blinkxLedOn = false;
unsigned long blinkxPreviousMillis = 0;
const uint32_t blinkxOnMs = 200;
const uint32_t blinkxOffMs = 200;

// -----------------------------------------------------------------------------
// Incoming line parser for ESP32-C6 -> Arduino direction
// -----------------------------------------------------------------------------

const size_t RX_LINE_LEN = 96;
char rxLine[RX_LINE_LEN];
size_t rxPos = 0;

static void setRgbLed(uint32_t color)
{
  rgbLed.setPixelColor(0, color);
  rgbLed.show();
}

static void startBlinkx(int count)
{
  if (count < 0) {
    count = 0;
  }
  if (count > 50) {
    count = 50;
  }

  blinkxTogglesRemaining = count * 2;
  blinkxLedOn = false;
  blinkxPreviousMillis = millis();

  setRgbLed(BLINKX_OFF);
}

static void serviceBlinkx()
{
  if (blinkxTogglesRemaining <= 0) {
    if (blinkxLedOn) {
      blinkxLedOn = false;
      setRgbLed(BLINKX_OFF);
    }
    return;
  }

  unsigned long now = millis();
  uint32_t interval = blinkxLedOn ? blinkxOnMs : blinkxOffMs;

  if (now - blinkxPreviousMillis >= interval) {
    blinkxPreviousMillis = now;
    blinkxLedOn = !blinkxLedOn;
    blinkxTogglesRemaining--;

    setRgbLed(blinkxLedOn ? BLINKX_BLUE : BLINKX_OFF);
  }
}

static void handleEsp32Line(const char *line)
{
  if (!line || line[0] == '\0') {
    return;
  }

  // Expected from Zigbee bridge:
  //   "Kitchen blinkx 7"
  //
  // The Arduino trusts Zigbee routing, so it discards the first token
  // without checking whether the name is actually "Kitchen".

  char name[24] = {0};
  char cmd[24] = {0};
  int value = 0;

  int fields = sscanf(line, "%23s %23s %d", name, cmd, &value);

  if (fields >= 2 && strcmp(cmd, "blinkx") == 0) {
    if (fields < 3) {
      value = 1;
    }

    Serial.print("[Arduino] blinkx ");
    Serial.println(value);

    startBlinkx(value);
  }
}

static void processEsp32Byte(char ch)
{
  // Always preserve the transparent bridge behavior to PuTTY.
  Serial.write(ch);

  if (ch == '\r' || ch == '\n') {
    if (rxPos == 0) {
      return;
    }

    rxLine[rxPos] = '\0';
    handleEsp32Line(rxLine);

    rxPos = 0;
    memset(rxLine, 0, sizeof(rxLine));
  }
  else if (rxPos < sizeof(rxLine) - 1) {
    rxLine[rxPos++] = ch;
  }
  else {
    // Line too long. Drop parser buffer, but keep bridge forwarding alive.
    rxPos = 0;
    memset(rxLine, 0, sizeof(rxLine));
    Serial.println();
    Serial.println("[Arduino] RX line too long, parser buffer cleared");
  }
}

void setup() {
  // Initialize the simple built-in LED used by your original pattern.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, ledState);

  // Initialize the built-in addressable RGB LED used by blinkx.
  rgbLed.begin();
  rgbLed.setBrightness(40);
  setRgbLed(BLINKX_OFF);

  // Initialize the USB Serial port (communication with PuTTY / PC).
  Serial.begin(9600);

  // Initialize the Hardware UART port (communication with the ESP32-C6).
  // On your wiring: Arduino RX/TX lines go to the ESP32-C6 bridge pins.
  Serial1.begin(9600);
}

void loop() {
  // ---------------------------------------------------------
  // 1. Existing non-blocking LED_BUILTIN blink routine
  // ---------------------------------------------------------
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= blinkPattern[patternIndex]) {
    previousMillis = currentMillis;
    patternIndex = (patternIndex + 1) % patternLength;

    ledState = (patternIndex % 2 == 0) ? HIGH : LOW;
    digitalWrite(LED_BUILTIN, ledState);
  }

  // ---------------------------------------------------------
  // 2. New non-blocking blinkx routine on the addressable RGB LED
  // ---------------------------------------------------------
  serviceBlinkx();

  // ---------------------------------------------------------
  // 3. Serial Bridge Routine
  // ---------------------------------------------------------

  // USB to Hardware: If data comes from PuTTY, send it to the ESP32-C6.
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }

  // Hardware to USB: If data comes from the ESP32-C6, forward it to PuTTY
  // and also parse complete lines for "Name blinkx N".
  while (Serial1.available()) {
    processEsp32Byte((char)Serial1.read());
  }
}
