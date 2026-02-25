# Seed WiFi

ESP32 device that pretends to be a USB keyboard and can be controlled remotely over WireGuard VPN.

## What it does

Plug this into a computer and it shows up as a regular USB keyboard. Send it an HTTP request over WireGuard, and it types whatever you tell it to—character by character, like someone's actually pressing the keys.

## Features

- Shows up as a USB keyboard to the computer
- Connects to WiFi and sets up a WireGuard tunnel
- HTTP API for sending keystrokes (POST /keys with text)
- Optional auth token protection
- Can type debug messages to show what it's doing

## Hardware Requirements

- ESP32 board with USB HID support (e.g., ESP32-S2, ESP32-S3, ESP32-C3)
- Tested on: Seeed XIAO ESP32S3
- USB cable for connection to target computer

## Dependencies

- WiFi (built-in ESP32 library)
- WebServer (ESP32 library)
- WireGuard-ESP32
- USB (ESP32 USB library)
- USBHIDKeyboard (ESP32 USB HID library)

## Configuration

All configuration is done via preprocessor defines passed as compiler flags during build.

### Required Settings

| Define | Description | Example |
|--------|-------------|---------|
| `WIFI_SSID` | WiFi network name | `"MyNetwork"` |
| `WIFI_PASS` | WiFi password | `"mypassword"` |
| `WG_PRIVATE_KEY` | WireGuard private key for this device | `"xtx6P88O20fV..."` |
| `WG_LOCAL_IP` | Virtual IP address for this device | `"100.64.100.21"` |
| `WG_PEER_PUBLIC_KEY` | Public key of the WireGuard peer/server | `"fRyToqy55q/L..."` |
| `WG_PEER_HOST` | Hostname or IP of WireGuard peer | `"wg.example.com"` |
| `WG_PEER_PORT` | WireGuard peer port | `51820` |

### Optional Settings

| Define | Description | Default |
|--------|-------------|---------|
| `HTTP_AUTH_TOKEN` | Authentication token for HTTP API | `""` (no auth) |

## Building and Uploading

Using arduino-cli with the XIAO ESP32S3:

```bash
arduino-cli compile -b esp32:esp32:XIAO_ESP32S3 \
  --build-property compiler.cpp.extra_flags="-DWIFI_SSID=\"YourSSID\" \
  -DWIFI_PASS=\"YourPassword\" \
  -DWG_PRIVATE_KEY=\"YourPrivateKey\" \
  -DWG_LOCAL_IP=\"100.64.100.21\" \
  -DWG_PEER_PUBLIC_KEY=\"PeerPublicKey\" \
  -DWG_PEER_HOST=\"wg.example.com\" \
  -DWG_PEER_PORT=51820 \
  -DHTTP_AUTH_TOKEN=\"yoursecret\"" \
  /path/to/seed_wifi

arduino-cli upload -b esp32:esp32:XIAO_ESP32S3 -p /dev/ttyACM0 /path/to/seed_wifi
```

## API Endpoints

### POST /keys
Types the provided text through the USB keyboard interface.

**Request:**
```http
POST /keys HTTP/1.1
X-Auth: <token>
Content-Type: application/x-www-form-urlencoded

keys=<text_to_type>
```

**Response:**
- `200 OK` - Text was typed successfully
- `401 Unauthorized` - Invalid or missing auth token

**Example:**
```bash
curl -X POST http://100.64.100.21/keys \
  -H "X-Auth: yoursecret" \
  -d "keys=echo hello world"
```

### GET /health
Returns device status information.

**Response:**
```
wifi=up|down
wifi_ip=<local_wifi_ip>
wg_ip=<wireguard_ip>
peer=<peer_host>:<peer_port>
```

**Example:**
```bash
curl http://100.64.100.21/health
```

## How it works

On boot:
1. USB keyboard interface initializes (waits 2.5s so you can click into a terminal/editor if you want to see debug output)
2. Connects to WiFi
3. Syncs time via NTP (WireGuard needs accurate time)
4. Establishes WireGuard tunnel
5. Starts HTTP server on port 80, listening on the WireGuard IP

Then it just sits there. Send a POST to `/keys` and it types whatever you gave it.

## Debug mode

Set `debugEnabled = true` in seed_wifi.ino:173 to make the device type status messages (prefixed with `#`) as it boots and operates. Useful for troubleshooting, but you probably don't want random debug text showing up on production systems.

## Security notes

This thing can type anything on the computer it's plugged into. Treat it like you would root access.

- WireGuard encrypts the connection
- Set `HTTP_AUTH_TOKEN` if you want an extra auth layer
- Keep the WireGuard private key secret
- Anyone with physical access can reflash it
- Only use this on systems you own or have written permission to access

## Why would you use this?

- Remote admin when SSH isn't available
- Authorized security testing
- Automating keyboard input on systems with no network API
- Emergency access when other remote methods are broken
- Testing how systems handle keyboard input at a hardware level

## Troubleshooting

**WiFi won't connect** - Double-check the SSID and password in your compile flags

**WireGuard fails** - Make sure NTP sync worked (check debug output). Also verify peer config is correct.

**Computer doesn't see it as a keyboard** - Not all ESP32 boards support USB HID. You need an S2, S3, or C3.

**HTTP requests time out** - Check that the WireGuard tunnel is actually up and you're hitting the right IP

## License

See project license file for details.
