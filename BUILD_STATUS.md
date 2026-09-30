# v3 build and validation status

## Target build status

**NOT COMPILED OR LINKED FOR ESP32-S3. NOT FLASHED OR HARDWARE-TESTED.**

Arduino CLI / ESP32-S3 toolchains were not present in the execution environment. A download attempt failed because the toolchain download host could not be resolved. No `.bin` or `.elf` is supplied. Do not interpret desktop compilation, API header review, simulation or browser rendering as a successful Arduino build.

Target dependencies retained from v2: Arduino-ESP32 3.3.12, NimBLE-Arduino 2.5.1 and U8g2 2.37.1. New modules use built-in WiFi, WebServer and Preferences. Public upstream interfaces were checked, but only a real target build can establish compile/link compatibility.

## Checks actually executed

Run from the project root:

```bash
python3 tools/embed_web.py
bash tests/run_tests.sh
bash tests/run_mock_tests.sh
bash tests/run_web_tests.sh
node --check web/app.js
python3 tests/test_browser.py
```

| Check | Result | Scope |
|---|---|---|
| Portable core | 30 passed | C++ logic, menus, three defaults, gestures, repeats, time wrap, 100,000-step random state checks |
| HID descriptor | Passed | Keyboard input 8 bytes, keyboard output 1 byte, media input 2 bytes; length only |
| Default hardware gate | 3 passed | Shipping PINMAP_VERIFIED=false prevents buttons, BLE and Wi-Fi setup |
| Mocked application | 41 passed | Actual application/BLE/core/web/storage sources against API test doubles |
| Config/codec/storage | 27 passed | Mapping validation, portable encoding, CRC, corruption, integer bounds, mocked write/readback and reload |
| Web endpoints | 35 passed | Actual handlers with mocked HTTP/Wi-Fi, validation, token/origin, stale revisions, save/reset/exit, timeout |
| JavaScript syntax | Passed | Node parser, no execution or browser compatibility claim |
| Chromium UI | 21 passed | Actual page assets, desktop and 390 px mobile viewport, simulated API/confirmation responses |

Output is retained in `tests/test_results.txt`, `mock_test_results.txt`, `web_test_results.txt` and `browser_test_results.txt`. Screenshots are in `tests/browser/`. The fixtures are generated from test handler responses and contain deterministic mock session values, not device credentials.

## Mock and browser limitations

The mock application builds actual App.cpp, BleRemote.cpp, RemoteCore.cpp, KeyCatalog.cpp, ConfigCodec.cpp, ConfigStore.cpp and WebSetup.cpp using a desktop C++ compiler with strict warnings. Arduino, NimBLE, GPIO, OLED, Preferences, Wi-Fi and WebServer are replaced by explicit test doubles. A temporary source copy changes PINMAP_VERIFIED for integration tests; the shipping source remains false.

The browser environment blocked URL navigation. Tests therefore loaded the actual local HTML/CSS/JS into Chromium in memory and supplied simulated fetch responses and confirmation decisions. They did not change browser policy. This validates DOM/event behavior and viewport layout, not real HTTP delivery, CSP enforcement, Wi-Fi association, native confirmation presentation, iOS/Android support or embedded resource limits.

A real profile-switching bug was found by these tests: a detached input's blur/change event could reach a newly selected profile draft during DOM replacement. The page now suppresses events while rendering and ignores detached event targets; switching/discarding drafts and profile isolation are covered by regression checks.

No real flash power-cut behavior, battery duration, BLE/Wi-Fi coexistence, reconnect stability, host shortcut compatibility, RF performance or production qualification has been measured. Browser snapshots show simulated configuration data, not a connection to a physical device.

## Before shipping hardware

Verify the exact board pin map and upload path; compile with real target libraries; exercise normal three-profile behavior; connect to the AP from both a phone and computer; save/reload/reboot; test all key types and modifiers; verify no control leakage on setup entry/exit or A+B; test AP failure, timeout and flash errors; verify media mute and presentation keys on intended host applications. Keep independent recovery firmware and validated board settings.
