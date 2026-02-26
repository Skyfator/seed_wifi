#include <WiFi.h>
#include <WebServer.h>
#include <WireGuard-ESP32.h>
#include <ESP32Servo.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;
static WireGuard wg;
WebServer server(80);
Servo myServo;

const int SERVO_PIN  = 9;
const int US_MIN     = 500;
const int US_MAX     = 2500;
const int START_US   = US_MIN + (int)(7.0  * 2000.0 / 180.0);
const int END_US     = US_MIN + (int)(30.0 * 2000.0 / 180.0);
const unsigned long SWEEP_MS = 3000;

void sweepToEnd() {
  unsigned long t0 = millis();
  while (true) {
    unsigned long elapsed = millis() - t0;
    if (elapsed >= SWEEP_MS) break;
    float progress = (float)elapsed / SWEEP_MS;
    int us = START_US + (int)(progress * (END_US - START_US));
    myServo.writeMicroseconds(us);
    delay(20);
  }
  myServo.writeMicroseconds(END_US);
}

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#ifndef WIFI_SSID
  #define WIFI_SSID "essid"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "wifipass"
#endif
#ifndef WG_PRIVATE_KEY
  #define WG_PRIVATE_KEY "YOUR_PRIVATE_KEY_HERE"
#endif
#ifndef WG_LOCAL_IP
  #define WG_LOCAL_IP "100.64.0.10"
#endif
#ifndef WG_PEER_PUBLIC_KEY
  #define WG_PEER_PUBLIC_KEY "PEER_PUBLIC_KEY_HERE"
#endif
#ifndef WG_PEER_HOST
  #define WG_PEER_HOST "wg.domain.tld"
#endif
#ifndef WG_PEER_PORT
  #define WG_PEER_PORT 51820
#endif
#ifndef HTTP_AUTH_TOKEN
  #define HTTP_AUTH_TOKEN ""
#endif

static const bool ADD_ENTER_AFTER_KEYS = true;

#define LED_PIN 21
void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH); delay(onMs);
    digitalWrite(LED_PIN, LOW);  delay(offMs);
  }
}

void typeRaw(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    Keyboard.write((uint8_t)s[i]);
  }
}

bool authOk() {
  if (String(HTTP_AUTH_TOKEN).length() == 0) return true;
  if (!server.hasHeader("X-Auth")) return false;
  return server.header("X-Auth") == String(HTTP_AUTH_TOKEN);
}

void handleKeys() {
  if (!authOk()) { server.send(401, "text/plain", "unauthorized\n"); return; }
  String keys = server.arg("keys");
  if (keys.length() == 0) { server.send(400, "text/plain", "missing keys param\n"); return; }
  keys.replace("\r", "");
  keys.replace("\n", "");
  typeRaw(keys);
  if (ADD_ENTER_AFTER_KEYS) Keyboard.write('\n');
  server.send(200, "text/plain", "ok\n");
}

void handlePowerOn() {
  if (!authOk()) { server.send(401, "text/plain", "unauthorized\n"); return; }
  server.send(200, "text/plain", "ok\n");
  // pass 1: sweep to 30°, snap back
  sweepToEnd();
  myServo.writeMicroseconds(START_US);
}

void handlePowerOff() {
  if (!authOk()) { server.send(401, "text/plain", "unauthorized\n"); return; }
  server.send(200, "text/plain", "ok\n");
  // pass 2: sweep to 30°, hold 11s, snap back
  sweepToEnd();
  delay(11000);
  myServo.writeMicroseconds(START_US);
}

void handleHealth() {
  String msg;
  msg += "wifi="    + String(WiFi.isConnected() ? "up" : "down") + "\n";
  msg += "wifi_ip=" + WiFi.localIP().toString() + "\n";
  msg += "wg_ip="   + String(WG_LOCAL_IP) + "\n";
  msg += "peer="    + String(WG_PEER_HOST) + ":" + String(WG_PEER_PORT) + "\n";
  server.send(200, "text/plain", msg);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  myServo.attach(SERVO_PIN, US_MIN, US_MAX);
  myServo.writeMicroseconds(START_US);
  delay(500);

  USB.begin();
  Keyboard.begin();

  delay(2500);

  // scan first: fast blink while scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  bool ssidFound = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == String(WIFI_SSID)) { ssidFound = true; break; }
  }
  // result: 3 fast = SSID found; 10 fast = SSID NOT found
  ledBlink(ssidFound ? 3 : 10, 80, 80);
  delay(1000);

  // fast blink = trying WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (!WiFi.isConnected()) {
    digitalWrite(LED_PIN, HIGH); delay(150);
    digitalWrite(LED_PIN, LOW);  delay(150);
  }
  ledBlink(3, 100, 100); // 3 quick flashes = WiFi up

  // slow blink = waiting for NTP
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  time_t now = 0;
  while (now < 1700000000) {
    digitalWrite(LED_PIN, HIGH); delay(500);
    digitalWrite(LED_PIN, LOW);  delay(500);
    now = time(nullptr);
  }
  ledBlink(3, 300, 150); // 3 slow flashes = NTP up

  IPAddress local;
  local.fromString(WG_LOCAL_IP);
  wg.begin(local, WG_PRIVATE_KEY, WG_PEER_HOST, WG_PEER_PUBLIC_KEY, (uint16_t)WG_PEER_PORT);

  server.collectHeaders(new const char*[1]{"X-Auth"}, 1);
  server.on("/power-on",  HTTP_POST, handlePowerOn);
  server.on("/power-off", HTTP_POST, handlePowerOff);
  server.on("/keys",      HTTP_POST, handleKeys);
  server.on("/health",    HTTP_GET,  handleHealth);
  server.onNotFound([]() { server.send(404, "text/plain", "not found\n"); });
  server.begin();

  digitalWrite(LED_PIN, HIGH); // solid = fully up
}

void loop() {
  server.handleClient();
}
