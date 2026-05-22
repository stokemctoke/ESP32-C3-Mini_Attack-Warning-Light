#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

extern volatile DeviceState  g_device_state;
extern SemaphoreHandle_t     g_state_mutex;

// Last completed detection-window snapshot — updated every g_detect_window ms.
// Written by detector_task; read by webserver handlers. Volatile uint32_t reads
// are atomic on ESP32, so no mutex is required.
extern volatile uint32_t g_last_deauth;
extern volatile uint32_t g_last_beacon;
extern volatile uint32_t g_last_probe;

// ── Packet log ────────────────────────────────────────────────────────────────
// Ring buffer of recent deauth/disassoc frames. Written by detector_task,
// read by webserver /log handler. No mutex — uint8 head/count are atomic;
// minor tearing in frame fields is acceptable for a display-only log.
#define PKT_LOG_SIZE 20

struct PacketLogEntry {
    uint32_t timestamp_ms;
    uint8_t  sa[6];     // sender MAC (the deauthenticator)
    uint8_t  bssid[6];  // target AP BSSID
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  subtype;   // 0x0C = deauth, 0x0A = disassoc
};

extern PacketLogEntry   g_pkt_log[PKT_LOG_SIZE];
extern volatile uint8_t g_pkt_log_head;   // next write index
extern volatile uint8_t g_pkt_log_count;  // filled entries, capped at PKT_LOG_SIZE

// Initialises WiFi promiscuous mode. Call after WiFi.softAP() has started.
void detector_init();

// FreeRTOS task entry point. Hops channels, counts frames, updates device state.
void detector_task(void* pvParameters);
