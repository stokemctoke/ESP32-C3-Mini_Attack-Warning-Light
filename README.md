# ESP32 Attack Warning Light

A USB-powered ambient desk light that passively monitors WiFi for 802.11 attack patterns. The light is the interface: smooth ambient effects under normal conditions, sharp visual alerts when a threat is detected.

---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-C3 Super Mini |
| LED strip | WS2812B (RGB), `LED_COUNT` LEDs |
| Data pin | GPIO8 via 300–500 Ω series resistor |
| Button | Momentary push-to-ground on GPIO9 (internal pull-up) |
| Power | USB 5V → VBUS for strip VCC; GPIO 3.3V for data |
| Decoupling cap | 100–1000 µF across strip VCC/GND rails |

```
ESP32-C3 Super Mini
  VBUS (5V) ─────────────────── WS2812B VCC
  GND       ─────────────────── WS2812B GND
  GPIO8 ── [300Ω] ────────────── WS2812B DIN
  GPIO9 ── [button] ─────────── GND
```

---

## Quick Start

1. Install **arduino-esp32** board package (Espressif Systems) in Arduino IDE.
2. Install **FastLED** library via Library Manager.
3. Select board: `ESP32C3 Dev Module` (or equivalent Super Mini variant).
4. Set `LED_COUNT` in `config.h` to match your strip.
5. Upload. The light starts in candle mode immediately.

---

## Configurable Constants (`config.h`)

| Constant | Default | Description |
|---|---|---|
| `LED_PIN` | 8 | GPIO to WS2812B data line |
| `LED_COUNT` | 12 | Number of LEDs on the strip |
| `LED_BRIGHTNESS` | 80 | Ambient brightness (0–255) |
| `BUTTON_PIN` | 9 | Mode-cycle button GPIO |
| `WIFI_CHANNEL` | 1 | Starting scan channel |
| `ALERT_COOLDOWN` | 10000 | ms to stay in alert after last detection |
| `DEAUTH_THRESHOLD` | 10 | Deauth/disassoc frames per window |
| `BEACON_THRESHOLD` | 20 | Beacon frames per window |
| `PROBE_THRESHOLD` | 15 | Probe request frames per window |
| `DETECTION_WINDOW` | 2000 | Rolling window duration (ms) |

---

## Ambient Modes (button cycles)

| Mode | Effect |
|---|---|
| Candle (default) | Per-LED independent warm flicker with occasional gust dip |
| Rainbow | Slow full-spectrum hue rotation (~12 s cycle) |
| Breathe | Warm white slow in/out pulse (~4 s cycle) |
| Forest | Slow green/teal palette drift |
| Ocean | Slow blue/cyan palette drift |

Short press cycles forward. Press during an alert is queued and applies when the alert clears. Mode survives power cycles (stored in flash).

---

## Alert States

| Trigger | Effect |
|---|---|
| Deauth / disassoc flood | Fast red strobe |
| Beacon flood | Rolling orange wave |
| Probe request flood | Slow amber throb |
| Two or more simultaneous | Alternating red/orange flash |

Detection uses WiFi promiscuous mode with channel hopping every 200 ms across channels 1–13. No network connection is made. After `ALERT_COOLDOWN` ms with no new detections the light crossfades back to ambient over 2 seconds.

---

## Open Questions (pre-build checks)

1. Confirm GPIO8 and GPIO9 are free on the specific Super Mini board variant in hand — some clones remap pins.
2. Confirm FastLED works with RMT on the C3 (`#define FASTLED_ESP32_I2S false` is already set in `config.h`; try removing if LEDs don't respond).
3. USB power: 500 mA (USB 2.0) is sufficient for up to ~8 LEDs at full white; at `LED_BRIGHTNESS 80` on candle mode 20+ LEDs are well within budget.
4. The white 150 ms ACK flash on mode change — replace with a brief brightness boost if the hard-cut is distracting in candle mode.

---

## Future (v2+)

- EAPOL 4-way handshake capture detection
- Evil twin / rogue AP detection (known-BSSID whitelist in NVS)
- WPS brute-force frame volume monitoring
- BLE advertising flood detection
- MQTT / HTTP push alerts to a local broker
- OLED display (attack type, count, timestamp)
- Web config portal (thresholds, brightness, custom ambient colours)
- 3D-printed diffuser shade
