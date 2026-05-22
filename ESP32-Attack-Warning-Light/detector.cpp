#include "detector.h"
#include "settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <WiFi.h>

// ── Frame counters (written by promiscuous callback, read by detector task) ───
static volatile uint32_t deauth_count  = 0;
static volatile uint32_t probe_count   = 0;
static volatile uint32_t beacon_count  = 0;

// Last completed window snapshot — exposed to web status endpoint.
volatile uint32_t g_last_deauth = 0;
volatile uint32_t g_last_beacon = 0;
volatile uint32_t g_last_probe  = 0;

// ── Alert history ring buffer ─────────────────────────────────────────────────
AlertHistoryEntry g_alert_hist[ALERT_HIST_SIZE];
volatile uint8_t  g_alert_hist_head  = 0;
volatile uint8_t  g_alert_hist_count = 0;

// ── Packet log ring buffer ────────────────────────────────────────────────────
PacketLogEntry   g_pkt_log[PKT_LOG_SIZE];
volatile uint8_t g_pkt_log_head  = 0;
volatile uint8_t g_pkt_log_count = 0;

// Single-entry staging: ISR writes here, detector_task drains to ring buffer.
// Last-wins if frames arrive faster than the task runs. Tearing in MAC bytes is
// acceptable — this is display-only, not a forensic record.
static struct {
    uint8_t  sa[6];
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  subtype;
    volatile bool ready;
} s_staging;

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
        // Stage frame metadata for the packet log (last-wins; consumed by detector_task)
        memcpy(s_staging.sa,    pkt->payload + 10, 6);
        memcpy(s_staging.bssid, pkt->payload + 16, 6);
        s_staging.rssi    = pkt->rx_ctrl.rssi;
        s_staging.channel = pkt->rx_ctrl.channel;
        s_staging.subtype = subtype;
        s_staging.ready   = true;
    } else if (subtype == 0x04) {
        probe_count  = probe_count  + 1;   // probe request
    } else if (subtype == 0x08) {
        beacon_count = beacon_count + 1;   // beacon — v1 uses raw count; unique-BSSID tracking is v2
    }
}

// ── State machine update (called after each detection window) ─────────────────
static uint32_t last_detection_ms = 0;

static void evaluate_thresholds(uint32_t deauth, uint32_t probe, uint32_t beacon) {
    bool att_deauth = deauth >= g_deauth_thresh;
    bool att_beacon = beacon >= g_beacon_thresh;
    bool att_probe  = probe  >= g_probe_thresh;
    int  active     = (int)att_deauth + (int)att_beacon + (int)att_probe;

    if (active > 0) {
        last_detection_ms = millis();

        DeviceState new_state;
        if      (active >= 2)  new_state = STATE_ALERT_MULTI;
        else if (att_deauth)   new_state = STATE_ALERT_DEAUTH;
        else if (att_beacon)   new_state = STATE_ALERT_BEACON;
        else                   new_state = STATE_ALERT_PROBE;

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) {
            DeviceState prev = g_device_state;
            g_device_state   = new_state;
            xSemaphoreGive(g_state_mutex);

            // Log on rising edge: new alert type, or returning from ambient
            if (prev == STATE_AMBIENT || prev == STATE_TRANSITIONING ||
                prev != new_state) {
                uint8_t idx = g_alert_hist_head;
                g_alert_hist[idx].timestamp_ms = millis();
                g_alert_hist[idx].alert_type   = (uint8_t)new_state;
                g_alert_hist_head  = (idx + 1) % ALERT_HIST_SIZE;
                if (g_alert_hist_count < ALERT_HIST_SIZE) g_alert_hist_count++;
            }
        }
        return;
    }

    // No attack this window — check cooldown
    DeviceState cur;
    if (!xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) return;
    cur = g_device_state;
    xSemaphoreGive(g_state_mutex);

    if (cur == STATE_AMBIENT || cur == STATE_TRANSITIONING) return;
    if (millis() - last_detection_ms < g_alert_cooldown) return;

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10))) {
        g_device_state = STATE_AMBIENT;
        xSemaphoreGive(g_state_mutex);
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────
// WiFi must already be running (started by webserver_init via WiFi.softAP).
void detector_init() {
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuous_cb);
    esp_wifi_set_channel(DEFAULT_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// ── Task ──────────────────────────────────────────────────────────────────────
void detector_task(void* pvParameters) {
    uint8_t  channel      = DEFAULT_WIFI_CHANNEL;
    uint32_t last_hop_ms  = millis();
    uint32_t window_start = millis();
    uint32_t last_log_ms  = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint32_t now = millis();

        // Drain ISR staging → packet log ring buffer (rate-limited to 1 entry/200 ms)
        if (s_staging.ready && now - last_log_ms >= 200) {
            s_staging.ready = false;
            uint8_t idx = g_pkt_log_head;
            g_pkt_log[idx].timestamp_ms = now;
            memcpy(g_pkt_log[idx].sa,    s_staging.sa,    6);
            memcpy(g_pkt_log[idx].bssid, s_staging.bssid, 6);
            g_pkt_log[idx].rssi    = s_staging.rssi;
            g_pkt_log[idx].channel = s_staging.channel;
            g_pkt_log[idx].subtype = s_staging.subtype;
            g_pkt_log_head = (idx + 1) % PKT_LOG_SIZE;
            if (g_pkt_log_count < PKT_LOG_SIZE) g_pkt_log_count++;
            last_log_ms = now;
        }

        // Channel hop — paused while a web client is connected so the AP stays
        // on a stable channel and the browser connection isn't disrupted.
        if (now - last_hop_ms >= g_channel_hop_ms) {
            if (WiFi.softAPgetStationNum() == 0) {
                channel = (channel % 13) + 1;
                esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            }
            last_hop_ms = now;
        }

        // Detection window
        if (now - window_start >= g_detect_window) {
            uint32_t snap_deauth  = deauth_count;  deauth_count  = 0;
            uint32_t snap_probe   = probe_count;   probe_count   = 0;
            uint32_t snap_beacon  = beacon_count;  beacon_count  = 0;
            window_start = now;

            g_last_deauth = snap_deauth;
            g_last_probe  = snap_probe;
            g_last_beacon = snap_beacon;

            evaluate_thresholds(snap_deauth, snap_probe, snap_beacon);
        }
    }
}
