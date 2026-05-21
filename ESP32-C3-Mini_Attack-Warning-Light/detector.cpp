#include "detector.h"
#include "esp_wifi.h"
#include "esp_event.h"

// ── Frame counters (written by promiscuous callback, read by detector task) ───
static volatile uint32_t deauth_count  = 0;
static volatile uint32_t probe_count   = 0;
static volatile uint32_t beacon_count  = 0;

// ── Promiscuous callback ───────────────────────────────────────────────────────
// Keep this as short as possible: increment counters only, no logic.
static void IRAM_ATTR promiscuous_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t* pkt =
        reinterpret_cast<const wifi_promiscuous_pkt_t*>(buf);
    uint8_t fc0        = pkt->payload[0];
    uint8_t frame_type = (fc0 >> 2) & 0x03;
    uint8_t subtype    = (fc0 >> 4) & 0x0F;

    if (frame_type != 0) return; // management frames only

    if (subtype == 0x0C || subtype == 0x0A) {
        deauth_count = deauth_count + 1;   // deauth (0x0C) or disassoc (0x0A)
    } else if (subtype == 0x04) {
        probe_count  = probe_count  + 1;   // probe request
    } else if (subtype == 0x08) {
        beacon_count = beacon_count + 1;   // beacon — v1 uses raw count; unique-BSSID tracking is v2
    }
}

// ── State machine update (called after each detection window) ─────────────────
static uint32_t last_detection_ms = 0;

static void evaluate_thresholds(uint32_t deauth, uint32_t probe, uint32_t beacon) {
    bool att_deauth = deauth >= DEAUTH_THRESHOLD;
    bool att_beacon = beacon >= BEACON_THRESHOLD;
    bool att_probe  = probe  >= PROBE_THRESHOLD;
    int  active     = (int)att_deauth + (int)att_beacon + (int)att_probe;

    if (active > 0) {
        last_detection_ms = millis();

        DeviceState new_state;
        if      (active >= 2)  new_state = STATE_ALERT_MULTI;
        else if (att_deauth)   new_state = STATE_ALERT_DEAUTH;
        else if (att_beacon)   new_state = STATE_ALERT_BEACON;
        else                   new_state = STATE_ALERT_PROBE;

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) {
            g_device_state = new_state;
            xSemaphoreGive(g_state_mutex);
        }
        return;
    }

    // No attack this window — check cooldown
    DeviceState cur;
    if (!xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) return;
    cur = g_device_state;
    xSemaphoreGive(g_state_mutex);

    if (cur == STATE_AMBIENT || cur == STATE_TRANSITIONING) return;
    if (millis() - last_detection_ms < ALERT_COOLDOWN) return;

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) {
        g_device_state = STATE_AMBIENT;
        xSemaphoreGive(g_state_mutex);
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────
void detector_init() {
    esp_event_loop_create_default(); // no-op if already created by Arduino core

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_start();
    // Hardware filter: only deliver management frames to the callback
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuous_cb);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// ── Task ──────────────────────────────────────────────────────────────────────
void detector_task(void* pvParameters) {
    uint8_t  channel      = WIFI_CHANNEL;
    uint32_t last_hop_ms  = millis();
    uint32_t window_start = millis();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint32_t now = millis();

        // Channel hop
        if (now - last_hop_ms >= CHANNEL_HOP_MS) {
            channel = (channel % 13) + 1;
            esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            last_hop_ms = now;
        }

        // Detection window
        if (now - window_start >= DETECTION_WINDOW) {
            // Snapshot and reset (slight race is acceptable for threshold detection)
            uint32_t snap_deauth  = deauth_count;  deauth_count  = 0;
            uint32_t snap_probe   = probe_count;   probe_count   = 0;
            uint32_t snap_beacon  = beacon_count;  beacon_count  = 0;
            window_start = now;

            evaluate_thresholds(snap_deauth, snap_probe, snap_beacon);
        }
    }
}
