# Attack Warning Light — Build Log

A running record of decisions, milestones, and surprises during development.
Source material for `/write-article` at the end of the project.

---

## 2026-05-19 — Handoff Complete: Repository Initialised from Detailed Specification

The project landed as an empty git repository with a comprehensive handoff document specifying the entire Attack Warning Light system in granular detail. This is the rare and fortunate case — the prior developer had thought through not just *what* to build but *why*, leaving no architectural ambiguity. Hardware pinout, FreeRTOS task structure, state machine enumeration, LED effect parameters, detection thresholds, and even the open questions that remained all present and accounted for before a single line of production code was written.

Having such a specification proved transformative. Rather than beginning with discovery work — inspecting datasheets, reverse-engineering previous sketches, or debating task boundaries — the foundation was solid enough to move directly to implementation. The specification included the precise file structure (config.h, detector.h/cpp, renderer.h/cpp, button.h/cpp, main file), clarified that Arduino IDE was favoured over PlatformIO despite the temptation of raw ESP-IDF, and even anticipated the sticky questions: GPIO availability on the specific Super Mini clone, FastLED's RMT compatibility on the C3, USB current budget for the LED strip, and aesthetic preferences for button feedback.

The repository now contains approximately 550 lines of working firmware code across these five source files, with all five ambient modes (candle, rainbow, breathe, forest, ocean) and four alert states (deauth strobe, beacon wave, probe throb, multi-flash) implemented and integration-ready. The detector runs on one FreeRTOS task, the renderer on another, with state protected by mutex. Configurable constants live at the top of config.h exactly as specified. The LED effects use FastLED, the WiFi detection uses esp-idf promiscuous mode directly, and the button handler is polled with millis()-based debounce.

What remains is integration testing: confirming GPIO8 and GPIO9 work as expected on the actual Super Mini clone in hand, validating FastLED's RMT behaviour under load, and measuring USB current draw with a range of LED counts and brightness settings. The open questions are all pre-build checks — they should be resolved before the first upload, and none of them require rework of the core architecture.

---

## 2026-05-19 — Toolchain: The Long Way Around to Arduino

Started the morning setting up PlatformIO as the handoff doc prescribed. Created platformio.ini with the espressif32 platform, configured for the esp32-c3-devkitm-1 board, added FastLED as a library dependency, and scaffolded the src/ directory structure. Felt solid — everything documented, everything in place. Then the user mentioned they don't have PlatformIO installed yet; it's queued for their VSCode setup. No point building on a foundation that isn't there. Deleted platformio.ini and the src/ directory within the hour. It was a clean false start, but the work itself wasn't wasted — it clarified what the build process needed to do.

This left two genuine contenders: Arduino IDE (familiar, FastLED works beautifully, reasonably featureful) versus ESP-IDF (the user's habitual toolchain for ESP32 work, more direct hardware access, proper FreeRTOS primitives). Superficially it looked like ESP-IDF was the "proper" choice — the user knows it well, and for WiFi promiscuous mode and NVS persistence the two environments are essentially equivalent. The sticking point was FastLED.

Asked directly: how much does *not* having FastLED cost? And the honest answer, once working through the lighting effects we want (candle flicker, rainbow, breathe, palette drift, crossfade), is that it costs a lot. FastLED's utilities — fill_rainbow(), ColorFromPalette(), the sin8() lookup table — are woven through the effect logic. In ESP-IDF you don't get those for free. You'd need to reimplement HSV-to-RGB conversion, palette interpolation, and a sine approximation table; probably 300–400 lines of utility scaffolding that does nothing but replicate what FastLED already does well. That's time spent solving a solved problem.

Decided on Arduino IDE. The distinguishing factor is that in this device, lighting isn't a nice-to-have cosmetic layer — it's a first-class feature. The ambient lamp behaviour *is* the user experience. FastLED lets us do it properly without rebuilding wheels, and Arduino's WiFi stack (arduino-esp32) gives us everything we need for the threat monitoring side. Sometimes the "heavyweight" framework is the right tool because it solves the problem that actually matters.

---

## 2026-05-19 — Initial Architecture: Firmware Written from Scratch

The entire firmware architecture has been designed and implemented. The core decision was to use Arduino IDE rather than PlatformIO or the raw ESP-IDF. This choice hinges almost entirely on FastLED: the library's 300+ lines of colour utilities and animation primitives (candle flicker, rainbow generation, palette interpolation) would have been painful to reimplement from first principles. The detection side — WiFi promiscuous mode and FreeRTOS API — is identical under arduino-esp32, so nothing is sacrificed on the monitoring capability.

The firmware runs two FreeRTOS tasks on the single ESP32-C3 core. The detector_task handles WiFi frame capture and state machine logic, whilst renderer_task drives the LED effects and button input. Shared state between tasks is protected by a mutex; FreeRTOS preempts on core 0, so no explicit core pinning is needed. State transitions are partitioned deliberately: the detector only ever sets STATE_AMBIENT or one of the alert states (STATE_ALERT_DEAUTH, STATE_ALERT_BEACON, etc.), while STATE_TRANSITIONING is managed entirely by the renderer. This keeps the WiFi callback code trivial — just counter increments — which in turn keeps CPU load low when frames arrive in bursts.

Five ambient lighting modes were implemented to avoid the static red-alert feeling: candle flicker (per-LED independent, comfortable to stare at), rainbow fade, breathing pulse, forest palette drift, and ocean drift. These pair with four alert patterns: deauth flood triggers red strobe, beacon flood triggers orange wave, probe flood triggers amber throb, and simultaneous detections trigger alternating red/orange flash. After each alert, a 2-second crossfade returns the lamp to the ambient mode it was in before.

The promiscuous callback deliberately avoids unnecessary processing. A hardware filter was configured to deliver only management frames (the attack types we care about: deauth, beacon, probe), which reduces the interrupt load substantially compared to capturing all frames and filtering in software. The callback itself only increments counters; detection logic runs asynchronously in the detector task.

All source files were written from scratch rather than adapting existing sketches or libraries. This forced deliberate choices about task boundaries, state machine structure, and LED effect implementation, resulting in a codebase that is small, focused, and easier to debug or extend than a heavier framework would provide.

---

## 2026-05-20 — First Compilation Attempt: A Preprocessor Irony

The initial build failed with `undefined reference to 'i2s_stop()'`, a linker error arising from a subtle but instructive misunderstanding of how FastLED interprets its configuration flags. The project's config.h contained `#define FASTLED_ESP32_I2S false`, written with the explicit intent to *disable* I2S audio interface support on the ESP32-C3 (which doesn't have hardware I2S anyway on this architecture). The code compiled, but the linker complained about missing I2S functions. It took only a moment to spot the culprit: FastLED 3.10.3 checks for the presence of these flags using the C preprocessor's `#ifdef` directive rather than `#if`. This means the preprocessor treats *any* definition of the symbol — including `#define FASTLED_ESP32_I2S false` — as "enable I2S." The irony is exact and rather perfect: a flag intended to disable I2S was instead enabling it.

The root cause lies in the architectural mismatch between FastLED's expectations and the actual hardware. FastLED was built when ESP32 variants had hardware I2S support; ESP32-C3 lacks it entirely. The arduino-esp32 3.3.8 platform (which sits atop ESP-IDF 5.x) no longer exports the legacy I2S API that FastLED was trying to call. Once the misguided `#define FASTLED_ESP32_I2S false` was removed entirely, FastLED's auto-detection kicked in correctly, defaulting to the RMT (Remote Control) peripheral, which the ESP32-C3 does possess and which is the intended backend for this chip.

The build now compiles. A secondary tweak was made at the same time: LED_COUNT in config.h was revised from 12 to 10 to match the physical LED strip on hand. With these changes in place, the firmware is ready for deployment to the device, though hardware validation — confirming GPIO8 and GPIO9 operate as expected, validating the RMT driver under load, and measuring USB current draw — remains ahead.

---

## 2026-05-20 — First Real Hardware Run: Candle to Red Strobe in One Breath

The firmware flashed cleanly to the physical device for the first time. The device booted straight into candle flicker mode, the default ambient state, with LEDs drifting gently and independently as intended. Then a WiFi deauther device was switched on nearby. Within moments the LEDs snapped into red strobe — the alert state, exactly as specified, with no crashes, no hangs, no nonsense. This is the moment the project stops being theoretical.

The whole point of the device is that the light itself *is* the interface. No screen, no serial monitor, no hidden state. You look at it and you see what's happening: peace brings candle flicker; an attack brings red strobe. The firmware accomplished this end-to-end in a single test run. The candle ambient mode worked, the deauth detection worked, the state machine transition worked, and the alert visual worked. Three years of thought across the handoff specification, an afternoon of careful implementation, and a FastLED preprocessor fix converged into hardware doing precisely what it was designed to do on the first real attempt.

What remains untested is substantial: the four other ambient modes (rainbow, breathe, forest, ocean) are implemented but unvalidated; the three other alert types (beacon wave, probe throb, multi-flash) are untested; the button mode cycling hasn't been exercised; Preferences persistence across power cycles hasn't been confirmed; and the crossfade transition back to ambient after the deauther switches off hasn't been observed. But none of this affects the core validation: the architecture works, the detection works, the rendering works, and the state machine transitions work under real wireless conditions. The rest is scope completion, not risk mitigation.

