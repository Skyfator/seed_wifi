#include <WiFi.h>
#include <WebServer.h>
#include <WireGuard-ESP32.h>

#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;
static WireGuard wg;
WebServer server(80);

// Config - override these with compiler flags
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

static const uint32_t TYPE_DELAY_MS = 400;   // small delay before each debug line
static const uint32_t STARTUP_GRACE_MS = 2500; // time to click into a window after reset
static const bool ADD_ENTER_AFTER_KEYS = true;

bool debugEnabled = false;

void dbg(const String& msg) {
  if (!debugEnabled) return;

  delay(TYPE_DELAY_MS);
  String m = msg;
  m.replace("\r", "");
  m.replace("\n", "");
  typeLine("# " + m);
}


// Keyboard typing helpers
void typeRaw(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    Keyboard.write((uint8_t)s[i]);
  }
}

void typeLine(const String& s) {
  typeRaw(s);
  Keyboard.write('\n');
}

bool authOk() {
  if (String(HTTP_AUTH_TOKEN).length() == 0) return true;
  if (!server.hasHeader("X-Auth")) return false;
  return server.header("X-Auth") == String(HTTP_AUTH_TOKEN);
}

void handleKeys() {
  if (!authOk()) {
    server.send(401, "text/plain", "unauthorized\n");
    dbg("HTTP /keys -> 401 unauthorized");
    return;
  }

  String keys = server.arg("keys");
  if (keys.length() == 0) {
    typeLine("# no wifi networks found");
    server.send(200, "text/plain", "typed fallback comment\n");
    dbg("HTTP /keys -> empty keys, typed fallback comment");
    return;
  }

  keys.replace("\r", "");
  keys.replace("\n", "");

  dbg("HTTP /keys -> typing " + String(keys.length()) + " chars");
  typeRaw(keys);
  if (ADD_ENTER_AFTER_KEYS) Keyboard.write('\n');

  server.send(200, "text/plain", "ok\n");
}

void handleHealth() {
  String msg;
  msg += "wifi=" + String(WiFi.isConnected() ? "up" : "down") + "\n";
  msg += "wifi_ip=" + WiFi.localIP().toString() + "\n";
  msg += "wg_ip=" + String(WG_LOCAL_IP) + "\n";
  msg += "peer=" + String(WG_PEER_HOST) + ":" + String(WG_PEER_PORT) + "\n";
  server.send(200, "text/plain", msg);
}

void connectWifi() {
  dbg(String("WiFi begin ssid=") + WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (!WiFi.isConnected()) {
    delay(500);
    // don't spam - only print every ~2s
    if ((millis() - t0) % 2000 < 600) dbg("WiFi connecting...");
  }

  dbg("WiFi OK ip=" + WiFi.localIP().toString());
}

void syncTime() {
  dbg("NTP sync start");
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  time_t now = 0;
  uint32_t t0 = millis();
  while (now < 1700000000) { // rough check that time is reasonable
    delay(300);
    now = time(nullptr);
    if (millis() - t0 > 12000) { // print something every ~12s so we know it's alive
      dbg("NTP still syncing...");
      t0 = millis();
    }
  }
  dbg("Time OK");
}

void startWireGuard() {
  dbg("WireGuard begin");
  IPAddress local;
  local.fromString(WG_LOCAL_IP);

  bool ok = wg.begin(
    local,
    WG_PRIVATE_KEY,
    WG_PEER_HOST,
    WG_PEER_PUBLIC_KEY,
    (uint16_t)WG_PEER_PORT
  );

  dbg(String("WireGuard begin result=") + (ok ? "OK" : "FAILED"));
}

void startHttp() {
  server.collectHeaders(new const char* [1]{"X-Auth"}, 1);
  server.on("/keys", HTTP_POST, handleKeys);
  server.on("/health", HTTP_GET, handleHealth);
  server.onNotFound([]() { server.send(404, "text/plain", "not found\n"); });

  server.begin();
  dbg("HTTP server started port=80");
  dbg(String("HTTP listen on WG ip=") + WG_LOCAL_IP);
}

void setup() {
  USB.begin();
  Keyboard.begin();

  // Give you time to click into a window to see debug output
  delay(STARTUP_GRACE_MS);
  dbg("Boot");

  connectWifi();
  syncTime();
  startWireGuard();
  startHttp();

  dbg("Ready");
}

void loop() {
  server.handleClient();
}
