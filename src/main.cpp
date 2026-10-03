#include <Arduino.h>
#include <Adafruit_DotStar.h>
#include <SPI.h>
#include "wiring_private.h"

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

// Bit-bang 115200 baud UART byte on a GPIO pin (48MHz SAMD21: 8.68us per bit)
static void bitbangTxByte(uint8_t pin, uint8_t b) {
  digitalWrite(pin, LOW);
  delayMicroseconds(8);
  for (int i = 0; i < 8; i++) {
    digitalWrite(pin, (b & (1 << i)) ? HIGH : LOW);
    delayMicroseconds(8);
  }
  digitalWrite(pin, HIGH);
  delayMicroseconds(9);
}

static void bitbangTxString(uint8_t pin, const char *s) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  delayMicroseconds(10);
  while (*s) {
    bitbangTxByte(pin, (uint8_t)*s++);
  }
}

static void sendReplyToBridge(const char *text) {
  // 1. Hardware UART Serial1 (Pin 4 on Trinket M0)
  Serial1.println(text);
  Serial1.flush();

  // 2. Also transmit on Pins 0 and 2 as fallback.
  // Note: Pin 1 (A0 / "1~") is dedicated to the SAMD21 Hardware DAC and must NOT be bit-banged!
  char buf[48];
  snprintf(buf, sizeof(buf), "%s\r\n", text);
  bitbangTxString(0, buf);
  bitbangTxString(2, buf);
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

  // Check for ping command (e.g. "Kitchen ping", "Kitchen pingx", or "ping")
  if ((fields >= 2 && (strcasecmp(cmd, "ping") == 0 || strcasecmp(cmd, "pingx") == 0)) ||
      (fields == 1 && (strcasecmp(name, "ping") == 0 || strcasecmp(name, "pingx") == 0))) {
    sendReplyToBridge("GotPing");

    // Local log to USB console
    Serial.println("[Arduino] ping received -> replied: GotPing");
    return;
  }

  // Check for setDAC command (e.g. "Kitchen setDAC 512" or "setDAC 512")
  int dacVal = -1;
  if (fields >= 2 && strcasecmp(cmd, "setDAC") == 0) {
    dacVal = (fields >= 3) ? value : 0;
  } else if (fields >= 1 && strcasecmp(name, "setDAC") == 0) {
    dacVal = (fields >= 2) ? atoi(cmd) : 0;
  }

  if (dacVal >= 0) {
    if (dacVal > 1023) {
      dacVal = 1023;
    }
    pinPeripheral(A0, PIO_ANALOG);
    analogWrite(A0, dacVal);

    char dacBuf[32];
    snprintf(dacBuf, sizeof(dacBuf), "GotDAC %d", dacVal);
    sendReplyToBridge(dacBuf);

    // Local debug print to USB console
    Serial.print("[Arduino] setDAC ");
    Serial.print(dacVal);
    Serial.print(" (approx ");
    Serial.print((dacVal * 3.3f) / 1023.0f, 2);
    Serial.println("V)");
    return;
  }

  int blinkCount = -1;
  if (fields >= 2 && strcasecmp(cmd, "blinkx") == 0) {
    blinkCount = (fields >= 3) ? value : 1;
  } else if (fields >= 1 && strcasecmp(name, "blinkx") == 0) {
    blinkCount = (fields >= 2) ? atoi(cmd) : 1;
  }

  if (blinkCount >= 0) {
    if (blinkCount <= 0) {
      blinkCount = 1;
    }

    Serial.print("[Arduino] blinkx ");
    Serial.println(blinkCount);

    startBlinkx(blinkCount);
    return;
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

  // Initialize the SAMD21 10-bit true hardware DAC on pin A0 (0..1023 -> 0..3.3V)
  analogWriteResolution(10);
  pinPeripheral(A0, PIO_ANALOG);
  analogWrite(A0, 0);

  // Initialize the USB Serial port (communication with PuTTY / PC).
  Serial.begin(9600);

  // Initialize the Hardware UART port (communication with the ESP32-C6).
  // On your wiring: Arduino RX/TX lines go to the ESP32-C6 bridge pins.
  Serial1.begin(115200);
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
