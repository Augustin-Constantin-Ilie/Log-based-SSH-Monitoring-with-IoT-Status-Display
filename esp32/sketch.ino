#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

// ---------- Configuration ----------
constexpr uint8_t  LCD_ADDR = 0x27, LCD_COLS = 16, LCD_ROWS = 2;
constexpr uint8_t  STRIP_PIN = 18, NUM_PIXELS = 10, BRIGHTNESS = 255;  // 0-255
constexpr uint32_t CRITICAL_MS   = 2000;  // stay red this long after the last alert
constexpr uint32_t BLINK_MS      = 400;
constexpr uint32_t WIFI_RETRY_MS = 5000;
constexpr uint32_t WIFI_TIMEOUT  = 15000;
const char* TOKEN = "!CHANGEME!";           // HTTP is unencrypted, local network only

enum State : uint8_t { UNKNOWN, STABLE, CRITICAL };

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
Adafruit_NeoPixel strip(NUM_PIXELS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

State        state = UNKNOWN;
bool         dirty = true, blinkOn = true;
unsigned int count = 0, rejected = 0;
uint32_t     lastAlert = 0, lastBlink = 0, lastWifiTry = 0;
char         detail[LCD_COLS + 1] = "";

// ---------- LED strip ----------
uint32_t stateColor() {
  switch (state) {
    case STABLE:   return strip.Color(0, 255, 0);
    case CRITICAL: return strip.Color(255, 0, 0);
    default:       return strip.Color(255, 140, 0);
  }
}

void showStrip(bool on) {
  strip.fill(on ? stateColor() : 0);
  strip.show();
}

// ---------- Display ----------
void printLine(uint8_t row, const char* text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof buf, "%-*.*s", LCD_COLS, LCD_COLS, text);  // pad and cut to width, no flicker
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void render() {
  char line[32];
  switch (state) {
    case STABLE:
      snprintf(line, sizeof line, "Alarms:%u", count);
      printLine(0, line);
      snprintf(line, sizeof line, "IP:%s", WiFi.localIP().toString().c_str());
      printLine(1, line);
      break;
    case CRITICAL:
      printLine(0, "!! CRITICAL !!");
      printLine(1, detail);
      break;
    default:
      printLine(0, "STATUS: UNKNOWN");
      printLine(1, "Connecting WiFi..");
  }
  blinkOn = true;
  lastBlink = millis();
  showStrip(true);
}

// ---------- States ----------
void setState(State s) {
  if (s == state) return;
  state = s;
  dirty = true;
}

void triggerCritical() {
  lastAlert = millis();
  state = CRITICAL;
  dirty = true;  // redraw even on repeated alerts (counter/text)
}

// ---------- Web server ----------
void handleAlert() {
  if (server.header("X-Token") != TOKEN) {
    rejected++;
    snprintf(detail, sizeof detail, "Wrong token x%u", rejected);
    triggerCritical();
    server.send(401, "text/plain", "no");
    return;
  }
  count++;
  snprintf(detail, sizeof detail, "SSH alarm #%u", count);
  triggerCritical();
  Serial.printf("Alarm #%u\n", count);
  server.send(200, "text/plain", "ok");
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  lcd.init();
  lcd.backlight();
  render();  // shows UNKNOWN at startup

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin("Wokwi-GUEST", "", 6); // !REMOVE if you're not using Private Iot Gateway or a Simulated Network!
  // Change to this -> WiFi.begin("SSID", "Password"); // It has to be an 2,4 GHz Network; Channel is automatically selected by the ESP32;
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT) delay(100);
  Serial.println(WiFi.localIP());

  const char* keys[] = {"X-Token"};
  server.collectHeaders(keys, 1);
  server.on("/alert", HTTP_POST, handleAlert);
  server.begin();
}

void loop() {
  server.handleClient();
  uint32_t now = millis();

  // Determine state
  if (WiFi.status() != WL_CONNECTED) {
    setState(UNKNOWN);
    if (now - lastWifiTry >= WIFI_RETRY_MS) {
      lastWifiTry = now;
      WiFi.reconnect();
    }
  } else if (state == UNKNOWN) {
    setState(STABLE);
  } else if (state == CRITICAL && now - lastAlert >= CRITICAL_MS) {
    setState(STABLE);
  }

  // Redraw only on change, blink without delay()
  if (dirty) {
    dirty = false;
    render();
  } else if (state == CRITICAL && now - lastBlink >= BLINK_MS) {
    lastBlink = now;
    blinkOn = !blinkOn;
    showStrip(blinkOn);
  }
  delay(1);
}