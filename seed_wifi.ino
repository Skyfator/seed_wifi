// seed_wifi — Remote Mac Mini control via USB HID + servo over WireGuard
// Fork of https://github.com/phokz/seed_wifi
//
// HTTP endpoints:
//   POST /keys        — type a string via USB HID keyboard
//   POST /power-on    — press power button (servo sweep + snap back)
//   POST /power-off   — hold power button 11s (force shutdown)
//   GET  /health      — connection status

#include <WiFi.h>
#include <WebServer.h>
#include <WireGuard-ESP32.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <ESP32Servo.h>

// ### Hardware

USBHIDKeyboard Keyboard;
static WireGuard wg;
WebServer server(80);
Servo powerServo;

// ### Servo config

const int  SERVO_PIN  = 9;       // D10 = GPIO9
const int  US_MIN     = 500;
const int  US_MAX     = 2500;
const int  START_US   = US_MIN + (int)(7.0  * 2000.0 / 180.0);  // 7°  resting
const int  END_US     = US_MIN + (int)(30.0 * 2000.0 / 180.0);  // 30° pressed
const unsigned long SWEEP_MS  = 3000;   // smooth press duration
const unsigned long HOLD_MS   = 11000;  // hold for force-off

// ### WiFi / WireGuard config — override via secrets.h or -D compiler flags

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

// ### Servo helpers

void servoSweepToEnd() {
  unsigned long t0 = millis();
  while (true) {
    unsigned long elapsed = millis() - t0;
    if (elapsed >= SWEEP_MS) break;
    float progress = (float)elapsed / SWEEP_MS;
    int us = START_US + (int)(progress * (END_US - START_US));
    powerServo.writeMicroseconds(us);
    delay(20);
  }
  powerServo.writeMicroseconds(END_US);
}

void servoReset() {
  powerServo.writeMicroseconds(START_US);
}

// ### Keyboard helpers

void typeRaw(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    Keyboard.write((uint8_t)s[i]);
  }
}

// ### Auth

bool authOk() {
  if (String(HTTP_AUTH_TOKEN).length() == 0) return true;
  if (!server.hasHeader("X-Auth")) return false;
  return server.header("X-Auth") == String(HTTP_AUTH_TOKEN);
}

// ### HTTP handlers

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

  servoSweepToEnd();
  servoReset();

  server.send(200, "text/plain", "power-on triggered\n");
}

void handlePowerOff() {
  if (!authOk()) { server.send(401, "text/plain", "unauthorized\n"); return; }

  servoSweepToEnd();
  delay(HOLD_MS);
  servoReset();

  server.send(200, "text/plain", "power-off triggered\n");
}

void handleHealth() {
  String msg;
  msg += "wifi="     + String(WiFi.isConnected() ? "up" : "down") + "\n";
  msg += "wifi_ip="  + WiFi.localIP().toString() + "\n";
  msg += "wg_ip="    + String(WG_LOCAL_IP) + "\n";
  msg += "peer="     + String(WG_PEER_HOST) + ":" + String(WG_PEER_PORT) + "\n";
  server.send(200, "text/plain", msg);
}

// ### Network setup

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (!WiFi.isConnected()) delay(500);
}

void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  time_t now = 0;
  while (now < 1700000000) { delay(300); now = time(nullptr); }
}

void startWireGuard() {
  IPAddress local;
  local.fromString(WG_LOCAL_IP);
  wg.begin(local, WG_PRIVATE_KEY, WG_PEER_HOST, WG_PEER_PUBLIC_KEY, (uint16_t)WG_PEER_PORT);
}

void startHttp() {
  server.collectHeaders(new const char*[1]{"X-Auth"}, 1);
  server.on("/keys",      HTTP_POST, handleKeys);
  server.on("/power-on",  HTTP_POST, handlePowerOn);
  server.on("/power-off", HTTP_POST, handlePowerOff);
  server.on("/health",    HTTP_GET,  handleHealth);
  server.onNotFound([]() { server.send(404, "text/plain", "not found\n"); });
  server.begin();
}

// ### Boot

void setup() {
  powerServo.attach(SERVO_PIN, US_MIN, US_MAX);
  powerServo.writeMicroseconds(START_US);

  USB.begin();
  Keyboard.begin();

  delay(2500);

  connectWifi();
  syncTime();
  startWireGuard();
  startHttp();
}

void loop() {
  server.handleClient();
}
