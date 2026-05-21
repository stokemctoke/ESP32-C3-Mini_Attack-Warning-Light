#include "renderer.h"
#include "button.h"
#include <FastLED.h>

static CRGB leds[LED_COUNT];

// ── Candle per-LED state ──────────────────────────────────────────────────────
struct CandleLED {
    uint8_t  current;     // current brightness
    uint8_t  target;      // target brightness
    uint8_t  step;        // brightness units per frame toward target
    CRGB     base;        // warm colour assigned once on seed
    uint32_t dip_until;   // millis() when dip recovery begins (0 = none)
};
static CandleLED candle[LED_COUNT];

static const CRGB CANDLE_PALETTE[3] = {
    CRGB(0xFF, 0xE8, 0xA0), // warm white
    CRGB(0xFF, 0xCC, 0x44), // yellow
    CRGB(0xFF, 0x8C, 0x00)  // amber
};

static void seed_candle() {
    for (int i = 0; i < LED_COUNT; i++) {
        candle[i].base     = CANDLE_PALETTE[i % 3];
        candle[i].current  = (uint8_t)((esp_random() % 80)  + 80);
        candle[i].target   = (uint8_t)((esp_random() % 215) + 40);
        candle[i].step     = (uint8_t)((esp_random() % 21)  + 5);
        candle[i].dip_until = 0;
    }
}

// ── Crossfade state ───────────────────────────────────────────────────────────
static CRGB        fade_from[LED_COUNT];
static uint32_t    fade_start_ms        = 0;
static AmbientMode fade_target          = AMBIENT_CANDLE;
static uint32_t    fade_duration_ms     = 2000;
static bool        fade_lerp_brightness = false; // true only for alert→ambient
static bool        fading_to_alert      = false;

// ── Effect functions ──────────────────────────────────────────────────────────

static void fx_candle() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    uint32_t now = millis();
    for (int i = 0; i < LED_COUNT; i++) {
        CandleLED& c = candle[i];

        // Dip recovery: pick a new high target
        if (c.dip_until > 0 && now >= c.dip_until) {
            c.target    = (uint8_t)((esp_random() % 135) + 120);
            c.step      = (uint8_t)((esp_random() % 21)  + 5);
            c.dip_until = 0;
        }

        // Step current brightness toward target
        if (c.current < c.target) {
            uint16_t nxt = (uint16_t)c.current + c.step;
            c.current = (nxt > c.target) ? c.target : (uint8_t)nxt;
        } else if (c.current > c.target) {
            uint8_t diff = c.current - c.target;
            c.current    = (c.step >= diff) ? c.target : c.current - c.step;
        }

        // Reached target — choose next
        if (c.current == c.target && c.dip_until == 0) {
            if ((esp_random() % 100) == 0) {
                // Gust dip: sudden drop to near-black, recover after 80-200 ms
                c.target    = (uint8_t)((esp_random() % 20) + 10);
                c.step      = 30;
                c.dip_until = now + (esp_random() % 120) + 80;
            } else {
                c.target = (uint8_t)((esp_random() % 215) + 40);
                c.step   = (uint8_t)((esp_random() % 21)  + 5);
            }
        }

        leds[i] = c.base;
        leds[i].nscale8_video(c.current);
    }
}

static void fx_rainbow() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    // Full hue cycle every 12 s
    uint8_t hue = (uint8_t)((millis() % 12000UL) * 255UL / 12000UL);
    fill_rainbow(leds, LED_COUNT, hue, 255 / LED_COUNT);
}

static void fx_breathe() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    // 4 s full breath cycle
    uint8_t phase = (uint8_t)((millis() % 4000UL) * 255UL / 4000UL);
    uint8_t bri   = sin8(phase);
    CRGB warm = CRGB(0xFF, 0xE8, 0xC0);
    warm.nscale8_video(bri);
    fill_solid(leds, LED_COUNT, warm);
}

static void fx_drift(CRGBPalette16 palette) {
    FastLED.setBrightness(LED_BRIGHTNESS);
    // Slow palette scroll: 8 s cycle, LEDs offset in phase
    uint8_t index = (uint8_t)((millis() % 8000UL) * 255UL / 8000UL);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t pos = index + (uint8_t)(i * 255 / LED_COUNT);
        leds[i] = ColorFromPalette(palette, pos, 200, LINEARBLEND);
    }
}

static void fx_kitt() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    const uint32_t STEP_MS    = 50;
    const uint32_t CYCLE_STEPS = 2 * (LED_COUNT - 1);
    const uint8_t  TAIL        = 4;

    int step = (int)((millis() / STEP_MS) % CYCLE_STEPS);
    int pos  = (step < LED_COUNT) ? step : (int)CYCLE_STEPS - step;
    int dir  = (step < LED_COUNT) ? 1 : -1;

    fill_solid(leds, LED_COUNT, CRGB::Black);
    for (int t = 1; t <= TAIL; t++) {
        int tailPos = pos - dir * t;
        if (tailPos >= 0 && tailPos < LED_COUNT) {
            uint8_t bri = 220 >> t; // 110, 55, 27, 13
            leds[tailPos] = CRGB(bri, 0, 0);
        }
    }
    leds[pos] = CRGB(255, 0, 0);
}

static float easeInOutCubic(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) / 2.0f;
}

static void fx_radial_breathe() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    const uint32_t BREATH_MS   = 4000;
    const uint32_t PAUSE_TOP   = 600;
    const uint32_t PAUSE_BOT   = 400;
    const uint32_t HALF_CYCLE  = (BREATH_MS - PAUSE_TOP - PAUSE_BOT) / 2;
    const float    EDGE_FALLOFF = 0.35f;   // outermost LED at 35% of centre brightness
    const float    LAG_MS       = 80.0f;   // ms lag per unit of distance from centre
    const float    CENTRE       = (LED_COUNT - 1) / 2.0f;
    // Warm amber-white
    const uint8_t R = 255, G = 210, B = 140;

    uint32_t phase = millis() % BREATH_MS;

    for (int i = 0; i < LED_COUNT; i++) {
        float distance = fabsf((float)i - CENTRE);
        float falloff  = 1.0f - (distance * (1.0f - EDGE_FALLOFF) / CENTRE);
        float lag      = distance * LAG_MS / (float)HALF_CYCLE;

        float t;
        if (phase < HALF_CYCLE) {
            t = (float)phase / (float)HALF_CYCLE - lag;
        } else if (phase < HALF_CYCLE + PAUSE_TOP) {
            t = 1.0f;
        } else if (phase < 2 * HALF_CYCLE + PAUSE_TOP) {
            t = 1.0f - (float)(phase - (HALF_CYCLE + PAUSE_TOP)) / (float)HALF_CYCLE - lag;
        } else {
            t = 0.0f;
        }
        t = constrain(t, 0.0f, 1.0f);

        float curved = easeInOutCubic(t);

        uint8_t bright = (uint8_t)(curved * 255.0f * falloff);

        leds[i] = CRGB(
            (uint8_t)((float)R * bright / 255.0f),
            (uint8_t)((float)G * bright / 255.0f),
            (uint8_t)((float)B * bright / 255.0f)
        );
    }
}

static void fx_plasma() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    uint16_t t = (uint16_t)(millis() / 8);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t v = sin8((uint8_t)(i * 12 + t))      / 2 +
                    sin8((uint8_t)(i * 23 - t * 2))   / 2;
        leds[i] = ColorFromPalette(LavaColors_p, v);
    }
}

static void fx_arc() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    fadeToBlackBy(leds, LED_COUNT, 80);
    if ((esp_random() % 8) == 0) {
        int start = (int)(esp_random() % (uint32_t)LED_COUNT);
        int len   = (int)(esp_random() % 4) + 1;
        for (int i = 0; i < len; i++) {
            int p = start + i;
            if (p < LED_COUNT) leds[p] = CRGB(180, 220, 255);
        }
    }
}

static void fx_fire_red() {
    FastLED.setBrightness(LED_BRIGHTNESS);
    uint16_t t = (uint16_t)(millis() * 2);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t n = inoise8((uint16_t)(i * 40), t);
        n = scale8(n, 160); // keep palette lookup in red-orange zone of HeatColors_p
        leds[i] = ColorFromPalette(HeatColors_p, n, 255, LINEARBLEND);
    }
}

static void fx_alert_deauth() {
    FastLED.setBrightness(255);
    // 3.6 Hz strobe (280 ms period)
    bool on = (millis() % 280UL) < 140UL;
    fill_solid(leds, LED_COUNT, on ? CRGB::Red : CRGB::Black);
}

static void fx_alert_beacon() {
    FastLED.setBrightness(255);
    // Orange rolling wave front-to-back, 400 ms full sweep
    uint8_t wave = (uint8_t)((millis() % 400UL) * LED_COUNT / 400UL);
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t dist = (uint8_t)abs((int)i - (int)wave);
        uint8_t bri;
        switch (dist) {
            case 0:  bri = 255; break;
            case 1:  bri = 150; break;
            case 2:  bri = 50;  break;
            default: bri = 0;   break;
        }
        CRGB c = CRGB(0xFF, 0x6B, 0x00);
        c.nscale8_video(bri);
        leds[i] = c;
    }
}

static void fx_alert_probe() {
    FastLED.setBrightness(255);
    // 1.4 Hz amber throb
    uint8_t phase = (uint8_t)((millis() % 1400UL) * 255UL / 1400UL);
    uint8_t bri   = sin8(phase);
    if (bri < 20) bri = 20; // don't fully black out
    CRGB amber = CRGB(0xFF, 0xBF, 0x00);
    amber.nscale8_video(bri);
    fill_solid(leds, LED_COUNT, amber);
}

static void fx_alert_multi() {
    FastLED.setBrightness(255);
    // 4.5 Hz alternating red / orange (220 ms period)
    bool phase = (millis() % 220UL) < 110UL;
    fill_solid(leds, LED_COUNT, phase ? CRGB::Red : CRGB(0xFF, 0x6B, 0x00));
}

// Dispatches to the correct ambient effect. Used for both normal render and
// crossfade target computation.
static void render_ambient(AmbientMode mode) {
    switch (mode) {
        case AMBIENT_CANDLE:  fx_candle();               break;
        case AMBIENT_RAINBOW: fx_rainbow();              break;
        case AMBIENT_BREATHE: fx_breathe();              break;
        case AMBIENT_FOREST:  fx_drift(ForestColors_p);  break;
        case AMBIENT_OCEAN:   fx_drift(OceanColors_p);   break;
        case AMBIENT_KITT:    fx_kitt();                 break;
        case AMBIENT_RADIAL:  fx_radial_breathe();       break;
        case AMBIENT_PLASMA:  fx_plasma();               break;
        case AMBIENT_ARC:     fx_arc();                  break;
        case AMBIENT_FIRE:    fx_fire_red();              break;
        default:              fx_candle();               break;
    }
}

// Returns true when the fade is complete.
static bool fx_crossfade() {
    uint32_t elapsed = millis() - fade_start_ms;
    if (elapsed >= fade_duration_ms) return true;

    uint8_t blend = (uint8_t)(elapsed * 255UL / fade_duration_ms);

    // Render ambient target into leds[], then capture it
    render_ambient(fade_target);
    CRGB ambient_snap[LED_COUNT];
    memcpy(ambient_snap, leds, sizeof(leds));

    // Blend: fade_from → ambient target
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = fade_from[i];
        nblend(leds[i], ambient_snap[i], blend);
    }

    // Alert→ambient only: interpolate brightness 255 → LED_BRIGHTNESS
    if (fade_lerp_brightness) {
        FastLED.setBrightness(lerp8by8(255, LED_BRIGHTNESS, blend));
    }
    return false;
}

// Fades from ambient snapshot into an alert effect over 250 ms.
// Renders the alert on the final frame so the caller can simply clear the flag.
static bool fx_crossfade_to_alert(DeviceState alert_state) {
    // Render alert target into leds[] (also serves as the final frame when done)
    switch (alert_state) {
        case STATE_ALERT_DEAUTH: fx_alert_deauth(); break;
        case STATE_ALERT_BEACON: fx_alert_beacon(); break;
        case STATE_ALERT_PROBE:  fx_alert_probe();  break;
        case STATE_ALERT_MULTI:  fx_alert_multi();  break;
        default: return true;
    }

    uint32_t elapsed = millis() - fade_start_ms;
    if (elapsed >= 250UL) return true; // alert already rendered above; we're done

    uint8_t blend = (uint8_t)(elapsed * 255UL / 250UL);
    CRGB alert_snap[LED_COUNT];
    memcpy(alert_snap, leds, sizeof(leds));

    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = fade_from[i];
        nblend(leds[i], alert_snap[i], blend);
    }
    FastLED.setBrightness(lerp8by8(LED_BRIGHTNESS, 255, blend));
    return false;
}

// ── Public API ────────────────────────────────────────────────────────────────

void renderer_init() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.setDither(BINARY_DITHER);
    FastLED.clear(true);
    seed_candle();
}

void renderer_task(void* pvParameters) {
    DeviceState prev_state = STATE_AMBIENT;
    bool        fading     = false;

    for (;;) {
        // Read shared state under mutex (release immediately)
        DeviceState state;
        AmbientMode mode;
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5))) {
            state = g_device_state;
            mode  = g_ambient_mode;
            xSemaphoreGive(g_state_mutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(16));
            continue;
        }

        // Abort ambient fade if a new alert fires
        if (fading && state != STATE_TRANSITIONING && state != STATE_AMBIENT) {
            fading = false;
        }

        // Detect ambient → alert edge: begin 250 ms fade-in
        bool now_alert   = (state != STATE_AMBIENT && state != STATE_TRANSITIONING);
        bool was_ambient = (prev_state == STATE_AMBIENT || prev_state == STATE_TRANSITIONING);
        if (now_alert && was_ambient && !fading_to_alert) {
            memcpy(fade_from, leds, sizeof(leds));
            fade_start_ms   = millis();
            fading_to_alert = true;
        }
        if (fading_to_alert && !now_alert) fading_to_alert = false;

        // Detect alert → ambient edge: begin crossfade
        bool was_alert = (prev_state != STATE_AMBIENT &&
                          prev_state != STATE_TRANSITIONING);
        if (was_alert && state == STATE_AMBIENT && !fading) {
            memcpy(fade_from, leds, sizeof(leds));
            fade_start_ms        = millis();
            fade_target          = mode;
            fade_duration_ms     = 2000;
            fade_lerp_brightness = true;
            fading               = true;
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5))) {
                g_device_state = STATE_TRANSITIONING;
                xSemaphoreGive(g_state_mutex);
            }
            state = STATE_TRANSITIONING;
        }

        // Button poll (renderer task owns the button)
        bool mode_changed = button_poll(state);
        if (mode_changed) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5))) {
                mode = g_ambient_mode;
                xSemaphoreGive(g_state_mutex);
            }
            // Crossfade from current frame (even mid-fade) to new mode
            memcpy(fade_from, leds, sizeof(leds));
            fade_start_ms        = millis();
            fade_target          = mode;
            fade_duration_ms     = 700;
            fade_lerp_brightness = false;
            fading               = true;
        }

        // Render
        if (fading) {
            bool done = fx_crossfade();
            if (done) {
                fading = false;
                if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5))) {
                    if (g_device_state == STATE_TRANSITIONING) {
                        g_device_state = STATE_AMBIENT;
                    }
                    xSemaphoreGive(g_state_mutex);
                }
            }
        } else if (fading_to_alert) {
            if (fx_crossfade_to_alert(state)) fading_to_alert = false;
        } else {
            switch (state) {
                case STATE_AMBIENT:
                case STATE_TRANSITIONING:
                    render_ambient(mode);
                    break;
                case STATE_ALERT_DEAUTH: fx_alert_deauth(); break;
                case STATE_ALERT_BEACON: fx_alert_beacon(); break;
                case STATE_ALERT_PROBE:  fx_alert_probe();  break;
                case STATE_ALERT_MULTI:  fx_alert_multi();  break;
            }
        }

        prev_state = state;
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 fps
    }
}
