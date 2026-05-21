#pragma once

// ── Hardware ──────────────────────────────────────────────────────────────────
#define LED_PIN         8       // GPIO to WS2812B data line
#define LED_COUNT       10      // change to match actual strip length
#define LED_BRIGHTNESS  80      // 0-255; keep low on USB power
#define BUTTON_PIN      9       // mode-cycle button (pull-up, active LOW)

// ── Detection ─────────────────────────────────────────────────────────────────
#define WIFI_CHANNEL        1       // starting scan channel (1-13)
#define ALERT_COOLDOWN      10000   // ms to stay in alert after last detection
#define DEAUTH_THRESHOLD    10      // deauth/disassoc frames per window to trigger alert
#define BEACON_THRESHOLD    50      // beacon frames per window (v1: raw count proxy)
#define PROBE_THRESHOLD     15      // probe request frames per window to trigger alert
#define DETECTION_WINDOW    2000    // ms rolling window for frame counting
#define CHANNEL_HOP_MS      200     // ms between channel hops (1-13)

// ── Button ────────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS     50

// ── Shared enums ──────────────────────────────────────────────────────────────
enum DeviceState : uint8_t {
    STATE_AMBIENT,
    STATE_ALERT_DEAUTH,
    STATE_ALERT_BEACON,
    STATE_ALERT_PROBE,
    STATE_ALERT_MULTI,
    STATE_TRANSITIONING   // crossfading back to ambient; managed by renderer only
};

enum AmbientMode : uint8_t {
    AMBIENT_CANDLE = 0,   // default on first boot
    AMBIENT_RAINBOW,
    AMBIENT_BREATHE,
    AMBIENT_FOREST,
    AMBIENT_OCEAN,
    AMBIENT_KITT,         // bouncing scanner with fading tail
    AMBIENT_RADIAL,       // radial breathe with centre-out wave and lag
    AMBIENT_PLASMA,       // layered sine fields mapped to lava palette
    AMBIENT_ARC,          // random blue-white electrical spark bursts
    AMBIENT_FIRE,         // perlin noise red fire — coherent flame body
    AMBIENT_MODE_COUNT    // sentinel for wrap-around
};
