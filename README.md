# DSTIKE HackHeld BLE Remote v3 — On-device Key Setup

Complete Arduino source project for the six-button, SH1106 128×64 OLED HackHeld32S3. Not the ST7789/HackHeld32C5 model. Do not mix source files from different versions.

**Development build: host and simulated browser checks pass, but no real ESP32-S3 compilation, linking, upload or physical BLE/Wi-Fi/flash verification has been completed. No firmware binary is supplied.**

## What changed

The boot menu now contains Phone, Computer and Web Setup. Computer still contains only Presentation and Media. The original three default layouts are retained; all 18 key mappings can now be edited in a local web page.

| Button | Phone Media | PC Presentation | PC Media |
|---|---|---|---|
| Up | Previous track | Page Up | Volume + |
| Down | Next track | Page Down | Volume − |
| Left | Volume + | Home | Previous track |
| Right | Volume − | End | Next track |
| A | Play/Pause | F5 | Play/Pause |
| B | Mute | Escape | Mute |

## Configure keys

1. Choose **Web Setup** on the remote, then press A.
2. Connect to the **DSTIKE-SETUP-XXXX** Wi-Fi network. Use the 12-character password displayed on the OLED. A new password is generated every setup session.
3. Open **http://192.168.4.1** manually. This local network has no Internet access, HTTPS or automatic captive-portal page.
4. Select one of the three profiles, edit its six cards, and click **Save this profile**.
5. Use **Exit setup** in the browser or press **B** on the remote. Choose Phone or Computer to resume BLE use.

BLE advertising/connection is paused during setup, and controls do not send HID reports. Wi-Fi turns off when leaving setup. A maximum of one Wi-Fi client is allowed. Setup exits after ten minutes without a page request; merely typing does not send keepalive traffic. Unsaved browser drafts are lost on exit/expiry.

Mappings can be media commands, one keyboard key plus optional Ctrl/Shift/Alt/Win-Cmd, or Disabled. Labels must contain 1–12 printable ASCII characters; the OLED overview shows the first ten. Labels are display names, not text to type. Repeating is optional for direction buttons only. A/B fire on release, and holding A+B for 1.2 seconds always goes Back. No scripts, macros, mouse control, browser-triggered HID, firmware upload or new remote profiles are added.

Save affects only the selected profile. Reset current/all restores and saves the relevant factory mappings. Stale revisions and invalid fields are rejected. Preferences stores a versioned, CRC-checked blob in namespace `hh-remote-v3`; writes are read back before success is reported. Invalid stored data falls back to defaults with a web status warning. A full flash erase also erases settings. Older v1 mappings are not migrated; web reset does not delete BLE bonds.

Phone/Computer selects a layout, not a specific bonded host. Only one BLE host is supported at a time. Mute is the HID media Mute command, not a phone ringer, Do Not Disturb or microphone switch. Actual actions depend on the host and focused application.

## Hardware gate — read before uploading

`BoardConfig.h` deliberately keeps `PINMAP_VERIFIED = false`. The device shows `PIN MAP CHECK` and does not start buttons, BLE or Wi-Fi until this is resolved.

Reference pins: OLED SDA/SCL 41/42; Up/Down/Left/Right/A/B 38/35/36/37/19/20; RGB 1; buzzers 5/40 (off). Verify your real module, schematic and known-working pin map before correcting the definitions and setting the flag true. GPIO35–37 can be reserved for octal memory; GPIO19/20 are native USB pins. Disabling PSRAM does not make module-reserved pins safe to reuse. Preserve your previously verified board configuration and upload route.

## Arduino target environment

Open `HackHeld_BLE_Remote/HackHeld_BLE_Remote.ino`, retaining all adjacent source files.

- ESP32 by Espressif Systems 3.3.12; ESP32S3 Dev Module.
- NimBLE-Arduino 2.5.1; U8g2 2.37.1.
- Built-in Wire, WiFi, WebServer and Preferences. No ArduinoJson, AsyncWebServer or BleKeyboard required.
- Reference memory: 16 MB flash, 16M Flash (3MB APP/9.9MB FATFS) partition; actual module configuration takes precedence.
- PSRAM Disabled; USB CDC/MSC/DFU on boot Disabled where present. Verify all hardware/upload settings.

These are targeted versions, not a claim of a successful target build. No filesystem upload is needed: HTML/CSS/JS are embedded in `WebAssets.h`. If editing `web/`, run `python3 tools/embed_web.py` before recompiling. Python is not needed for ordinary Arduino use of the supplied generated header.

Local HTTP setup uses session tokens, request-origin/host checks, strict field validation and revision checks. This is not a security audit or production certification. Do not expose the portal to the Internet or share its temporary Wi-Fi password with untrusted users.

## Verification

See `BUILD_STATUS.md` for commands and scope: 30 core tests, HID report-length checks, 3 hardware-gate checks, 41 mocked application checks, 27 configuration/storage checks, 35 HTTP-handler checks, JavaScript syntax check and 21 Chromium UI checks. Browser tests render the real page sources but replace network and confirmation responses with test doubles. Desktop/mobile viewport rendering is not a real phone or radio test.

Toolchain installation was attempted but unavailable because the download host could not be resolved. No real target build, physical pairing, network service or flash persistence validation has been performed. See `README_CN.md` for full Chinese instructions and primary API references.
