#include "App.h"
#include "BoardConfig.h"
#include "RemoteCore.h"
#include "BleRemote.h"
#include "ConfigStore.h"
#include "WebSetup.h"
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_arduino_version.h>
#include <esp32-hal-psram.h>
#include <esp32-hal-rgb-led.h>
#include <stdio.h>
#include <string.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select Tools > Board > ESP32S3 Dev Module. This firmware targets ESP32-S3."
#endif
#if ESP_ARDUINO_VERSION_MAJOR != 3
#error "Use esp32 by Espressif Systems 3.3.12 for this project; 2.x and 4.x are not targeted."
#endif

namespace {
using namespace remote;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, U8X8_PIN_NONE, board::OLED_SCL, board::OLED_SDA);
Debouncer buttons[ButtonCount];
ControlInput control;
Navigation navigation; // Phone / Computer / Web Setup; never auto-enters a saved mode
bool webPending = false, webFailed = false;
uint32_t webWaitAt = 0;
uint8_t pressedMask = 0;
bool uiGuard = true, fatal = false, displayPresent = false, bleStarted = false;
bool dirty = true, dimmed = false, priorChord = false;
uint32_t lastDraw = 0, lastActivity = 0, toastAt = 0, txAt = 0;
bool haveToast = false, txFlash = false;
uint32_t navAt[4]{};
bool navRepeating[4]{};
char toastText[26]{};
char deviceName[32]{};
char previousStatus[8]{};
PairKind previousPairKind = PairKind::None;
uint32_t previousPairStarted = 0;
uint8_t passDigits[6]{}, passCursor = 0;
ControlEvents controlEvents{};
uint32_t lastRgb = 0xFFFFFFFFu;

void logLine(const char* line) { if (board::UART_LOG) Serial0.println(line); }
void wakeDisplay(uint32_t now) {
    lastActivity = now;
    if (dimmed && displayPresent) { oled.setContrast(180); dimmed = false; dirty = true; }
}
void toast(const char* text, uint32_t now) {
    snprintf(toastText, sizeof(toastText), "%s", text);
    toastAt = now; haveToast = true; dirty = true;
}
void smallText(int x, int y, const char* text) {
    oled.setFont(u8g2_font_5x7_tf); oled.drawStr(x, y, text);
}
void title(const char* text) {
    oled.setFont(u8g2_font_6x10_tf); oled.drawStr(0, 9, text);
    smallText(102, 9, bleStatus()); oled.drawHLine(0, 11, 128);
}
void listRow(uint8_t row, bool selected, const char* text) {
    const int y = 27 + row * 14;
    if (selected) { oled.drawBox(0, y - 9, 128, 12); oled.setDrawColor(0); }
    smallText(4, y, text); oled.setDrawColor(1);
}
void failScreen(const char* heading, const char* a, const char* b, const char* c) {
    fatal = true; logLine(heading); logLine(a); logLine(b); logLine(c);
    if (!displayPresent) return;
    oled.clearBuffer();
    smallText(0, 10, heading); oled.drawHLine(0, 13, 128);
    smallText(0, 26, a); smallText(0, 38, b); smallText(0, 50, c);
    smallText(0, 63, "See README_CN.md"); oled.sendBuffer();
}
bool pinIsValid(int pin) { return (pin >= 0 && pin <= 21) || (pin >= 26 && pin <= 48); }
bool checkHardware() {
    // No button/RGB/buzzer GPIO is reconfigured until all checks pass.
    if (!board::PINMAP_VERIFIED) {
        failScreen("PIN MAP CHECK", "Verify BoardConfig.h", "GPIO35-37 / PSRAM", "GPIO19-20 / USB");
        return false;
    }
    const int pins[] = {board::BUTTON_PINS[0],board::BUTTON_PINS[1],board::BUTTON_PINS[2],
        board::BUTTON_PINS[3],board::BUTTON_PINS[4],board::BUTTON_PINS[5],board::OLED_SDA,
        board::OLED_SCL,board::RGB_PIN,board::BUZZER_PIN,board::SECOND_BUZZER_PIN};
    bool reservedUsed = false, usbUsed = false;
    for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i) {
        if (pins[i] < 0 && i >= 8) continue;
        if (!pinIsValid(pins[i]) || (pins[i] >= 26 && pins[i] <= 32)) {
            failScreen("INVALID GPIO", "Check all pin numbers", "Never use flash pins", "Fix BoardConfig.h"); return false;
        }
        for (size_t j = 0; j < i; ++j) if (pins[i] == pins[j]) {
            failScreen("DUPLICATE GPIO", "Two devices share a pin", "Check the schematic", "Fix BoardConfig.h"); return false;
        }
        reservedUsed |= pins[i] >= 33 && pins[i] <= 37;
        usbUsed |= pins[i] == 19 || pins[i] == 20;
        if (board::UART_LOG && (pins[i] == 43 || pins[i] == 44)) {
            failScreen("UART GPIO CONFLICT", "GPIO43/44 used twice", "Disable UART_LOG", "or correct the wiring"); return false;
        }
    }
    if (reservedUsed && psramFound()) {
        failScreen("PSRAM PIN CONFLICT", "GPIO33-37 are in use", "Do not remap active RAM", "Verify module + wiring"); return false;
    }
#if ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
    if (usbUsed) {
        failScreen("USB PIN CONFLICT", "Buttons use GPIO19/20", "Disable USB on boot", "Verify USB data wiring"); return false;
    }
#else
    (void)usbUsed;
#endif
    return true;
}
bool initDisplay() {
    Wire.begin(board::OLED_SDA, board::OLED_SCL);
    Wire.setClock(board::I2C_HZ); Wire.setTimeOut(25);
    uint8_t address = board::OLED_ADDRESS;
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
        address = address == 0x3C ? 0x3D : 0x3C;
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) return false;
    }
    oled.setI2CAddress(address << 1); // U8g2 uses the shifted (8-bit) address
    oled.setBusClock(board::I2C_HZ);
    if (!oled.begin()) return false;
    oled.setContrast(180); return true;
}

void guardInput() {
    bleCancel(); control.reset(); uiGuard = true;
    priorChord = false; controlEvents = {}; dirty = true;
}
void applyNavigation() {
    guardInput(); haveToast = false;
    if (navigation.screen() == Screen::WebSetup) {
        if (bleStarted) bleSetEnabled(false);
        webPending = true; webFailed = false; webWaitAt = millis();
        return; // wait for the existing BLE host to disconnect in bleTick
    }
    webStop(); webPending = webFailed = false;
    if (!navigation.wantsConnection()) {
        // Stop advertising and disconnect the old host before another selection.
        if (bleStarted) bleSetEnabled(false);
        return;
    }
    if (!bleStarted) {
        // Lazy initialization: nothing advertises at the first boot menu.
        if (!bleBegin(deviceName)) {
            failScreen("BLE START FAILED", "Check library versions", "esp32 3.3.12", "NimBLE-Arduino 2.5.1");
            return;
        }
        bleStarted = true;
    }
    bleSetEnabled(true);
}
uint8_t navEvents(uint32_t now) {
    uint8_t events = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        if (buttons[i].rose()) {
            events |= 1u << i; navAt[i] = now; navRepeating[i] = false;
        } else if (buttons[i].down() && elapsed(now, navAt[i],
                   navRepeating[i] ? REPEAT_INTERVAL_MS : REPEAT_DELAY_MS)) {
            events |= 1u << i; navAt[i] = now; navRepeating[i] = true;
        }
    }
    return events;
}
void stepIndex(uint8_t& index, uint8_t count, int delta) {
    index = static_cast<uint8_t>((index + count + delta) % count); dirty = true;
}
void processUi(uint32_t now, uint8_t nav) {
    if (navigation.screen() == Screen::WebSetup) {
        if (buttons[B].rose() && !buttons[A].down()) { navigation.back(); applyNavigation(); }
        return; // physical controls never generate HID reports during web setup
    }
    if (navigation.screen() != Screen::Run) {
        if (nav & (1u << Up)) { navigation.move(-1); dirty = true; }
        if (nav & (1u << Down)) { navigation.move(1); dirty = true; }
        const bool a = buttons[A].rose(), b = buttons[B].rose();
        if (a && !buttons[B].down()) {
            if (navigation.select()) applyNavigation();
        } else if (b && !buttons[A].down()) {
            if (navigation.back()) applyNavigation();
        }
        return;
    }
    const Profile& profile = getProfile(navigation.profile());
    controlEvents = control.update(pressedMask, getRepeatMask(navigation.profile()), now);
    if (controlEvents.chordActive) {
        if (!priorChord) bleCancel();
        dirty = true;
    }
    if (priorChord != controlEvents.chordActive) dirty = true;
    priorChord = controlEvents.chordActive;
    if (controlEvents.exit) {
        navigation.back(); applyNavigation(); return;
    }
    for (uint8_t i = 0; i < ButtonCount; ++i) {
        if (!(controlEvents.fireMask & (1u << i))) continue;
        const TxResult result = bleQueue(profile.actions[i]);
        if (result == TxResult::NotReady) toast("Not ready / no host", now);
        else if (result == TxResult::QueueFull) toast("Busy - try again", now);
    }
}
void processPair(const PairPrompt& prompt, uint8_t nav) {
    if (buttons[B].rose()) { blePairAnswer(false); uiGuard = true; return; }
    if (prompt.kind == PairKind::Compare && buttons[A].rose()) {
        blePairAnswer(true); uiGuard = true;
    } else if (prompt.kind == PairKind::Enter) {
        if (nav & (1u<<Left)) stepIndex(passCursor, 6, -1);
        if (nav & (1u<<Right)) stepIndex(passCursor, 6, 1);
        if (nav & (1u<<Up)) stepIndex(passDigits[passCursor], 10, 1);
        if (nav & (1u<<Down)) stepIndex(passDigits[passCursor], 10, -1);
        if (buttons[A].rose()) {
            uint32_t code = 0;
            for (uint8_t digit : passDigits) code = code*10+digit;
            blePairAnswer(true, code); uiGuard = true;
        }
    }
}
void drawPair(const PairPrompt& p) {
    title("BLE PAIRING");
    char digits[12];
    uint32_t pin = p.pin;
    if (p.kind == PairKind::Enter) { pin = 0; for (uint8_t digit : passDigits) pin = pin*10+digit; }
    snprintf(digits, sizeof(digits), "%06lu", static_cast<unsigned long>(pin));
    if (p.kind == PairKind::Waiting) {
        smallText(0, 28, "Finish on phone / PC"); smallText(0, 43, "Waiting for encryption");
        smallText(0, 63, "B:Cancel"); return;
    }
    smallText(0, 23, p.kind == PairKind::Compare ? "Do both numbers match?" :
                    p.kind == PairKind::Display ? "Enter on phone / PC" : "Enter phone / PC code");
    oled.setFont(u8g2_font_9x15_tf); oled.drawStr(37, 41, digits);
    if (p.kind == PairKind::Enter) oled.drawHLine(37+passCursor*9, 44, 8);
    smallText(0, 54, p.kind == PairKind::Enter ? "U/D:Digit L/R:Position" : deviceName);
    smallText(0, 63, p.kind == PairKind::Compare ? "A:Match  B:Reject" :
                    p.kind == PairKind::Enter ? "A:Submit B:Cancel" : "B:Cancel");
}

void drawScreen() {
    oled.clearBuffer(); oled.setDrawColor(1);
    const PairPrompt prompt = blePairPrompt();
    if (prompt.kind != PairKind::None) { drawPair(prompt); oled.sendBuffer(); return; }
    if (navigation.screen() == Screen::ChooseDevice) {
        title("CONNECT TO");
        listRow(0, navigation.selection() == 0, "Phone");
        listRow(1, navigation.selection() == 1, "Computer");
        listRow(2, navigation.selection() == 2, "Web Setup");
        smallText(0, 63, "U/D:Select  A:Confirm");
    } else if (navigation.screen() == Screen::ComputerModes) {
        title("COMPUTER");
        listRow(0, navigation.selection() == 0, "Presentation");
        listRow(1, navigation.selection() == 1, "Media");
        smallText(0, 54, deviceName);
        smallText(0, 63, "U/D:Select A:Open B:Back");
    } else if (navigation.screen() == Screen::WebSetup) {
        title("WEB SETUP");
        if (webFailed) {
            smallText(0, 27, "Setup could not start");
            smallText(0, 40, "B:Back, then retry");
        } else if (webPending) {
            smallText(0, 27, "Closing BLE connection");
            smallText(0, 40, "Starting setup Wi-Fi");
        } else {
            smallText(0, 23, webSsid());
            char credentials[24]; snprintf(credentials, sizeof(credentials), "PW: %s", webPassword());
            smallText(0, 34, credentials);
            smallText(0, 45, "http://192.168.4.1");
            smallText(0, 55, "BLE paused / local only");
        }
        smallText(0, 63, "B:Exit setup");
    } else {
        title(profileName(navigation.profile()));
        const Profile& profile = getProfile(navigation.profile());
        char text[32];
        for (uint8_t i = 0; i < ButtonCount; ++i) {
            const char* label = profile.actions[i].label;
            // Always display configured labels, not hard-coded presentation names.
            snprintf(text, sizeof(text), "%s:%.10s", shortButtonNames[i], label);
            smallText((i % 2) * 65, 23 + (i / 2) * 11, text);
        }
        if (controlEvents.chordActive) {
            oled.drawFrame(0, 50, 128, 6);
            oled.drawBox(1, 51, (126u * controlEvents.chordMs) / EXIT_HOLD_MS, 4);
        } else smallText(0, 55, haveToast ? toastText : (bleReady() ? "Ready for controls" : deviceName));
        smallText(0, 63, "Hold A+B 1.2s: Back");
    }
    oled.sendBuffer();
}
void updateRgb(uint32_t now) {
    if (board::RGB_PIN < 0) return;
    uint8_t r = 0, g = 0, b = 0;
    if (!dimmed && webActive()) { g = board::RGB_LEVEL; b = board::RGB_LEVEL; }
    else if (!dimmed && navigation.wantsConnection()) {
        if (blePairPrompt().kind != PairKind::None) { r = board::RGB_LEVEL; b = board::RGB_LEVEL; }
        else if (txFlash && !elapsed(now, txAt, 150)) { r = board::RGB_LEVEL; g = board::RGB_LEVEL / 2; }
        else if (bleReady()) g = board::RGB_LEVEL;
        else b = board::RGB_LEVEL;
    }
    const uint32_t color = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
    if (color != lastRgb) {
        rgbLedWriteOrdered(board::RGB_PIN, LED_COLOR_ORDER_GRB, r, g, b); lastRgb = color;
    }
}
}

void appSetup() {
    if (board::UART_LOG) Serial0.begin(115200);
    displayPresent = initDisplay();
    if (!displayPresent) { fatal = true; logLine("OLED not found at 0x3C/0x3D. Check GPIO41/42."); return; }
    if (!checkHardware()) return;
    configLoad(); // invalid/missing saved data falls back to the three v2 defaults
    for (uint8_t i = 0; i < remote::ButtonCount; ++i)
        pinMode(board::BUTTON_PINS[i], board::BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    // No sound feature; keep both buzzer pins inactive.
    if (board::BUZZER_PIN >= 0) { pinMode(board::BUZZER_PIN, OUTPUT); digitalWrite(board::BUZZER_PIN, LOW); }
    if (board::SECOND_BUZZER_PIN >= 0) { pinMode(board::SECOND_BUZZER_PIN, OUTPUT); digitalWrite(board::SECOND_BUZZER_PIN, LOW); }
    snprintf(deviceName, sizeof(deviceName), "%s-%04X", board::DEVICE_PREFIX,
             static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFFu));
    control.reset(); lastActivity = millis(); dirty = true;
    updateRgb(lastActivity); drawScreen(); lastDraw = millis();
}

void appLoop() {
    using namespace remote;
    if (fatal) { delay(30); return; }
    uint32_t now = millis();
    pressedMask = 0;
    for (uint8_t i = 0; i < ButtonCount; ++i) {
        const bool raw = digitalRead(board::BUTTON_PINS[i]) == (board::BUTTON_ACTIVE_LOW ? LOW : HIGH);
        buttons[i].update(raw, now, board::DEBOUNCE_MS);
        if (buttons[i].down()) pressedMask |= 1u << i;
        if (buttons[i].rose() || buttons[i].fell()) { wakeDisplay(now); dirty = true; }
    }
    bleTick(now); // no-op before the user's first selection
    if (navigation.screen() == Screen::WebSetup) {
        if (webPending && !bleConnected()) {
            webFailed = !webBegin(static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFu));
            webPending = false; wakeDisplay(millis()); dirty = true;
        } else if (webPending && elapsed(now, webWaitAt, 5000)) {
            webPending = false; webFailed = true; dirty = true;
        }
        webTick(now);
        if (webExitRequested()) { navigation.back(); applyNavigation(); }
        now = millis(); // HTTP handling may have consumed time
    }
    const PairPrompt prompt = blePairPrompt();
    if (prompt.kind != previousPairKind || prompt.started != previousPairStarted) {
        guardInput(); wakeDisplay(now);
        if (prompt.kind == PairKind::Enter && prompt.kind != previousPairKind) {
            memset(passDigits, 0, sizeof(passDigits)); passCursor = 0;
        }
        previousPairKind = prompt.kind; previousPairStarted = prompt.started;
    }
    const char* status = bleStatus();
    if (strcmp(previousStatus, status) != 0) {
        snprintf(previousStatus, sizeof(previousStatus), "%s", status);
        guardInput(); wakeDisplay(now);
    }
    char eventLabel[13];
    const int event = bleTakeEvent(eventLabel, sizeof(eventLabel));
    if (event == 1) {
        char message[26]; snprintf(message, sizeof(message), "Sent: %s", eventLabel);
        toast(message, now); txAt = now; txFlash = true;
    } else if (event == -1) toast(eventLabel, now);

    const uint8_t nav = navEvents(now);
    if (uiGuard) {
        if (!pressedMask) { uiGuard = false; control.update(0, 0, now); }
    } else if (prompt.kind != PairKind::None) processPair(prompt, nav);
    else processUi(now, nav);

    if (fatal) { delay(1); return; } // do not overwrite a BLE-start failure screen
    if (haveToast && elapsed(now, toastAt, 1600)) { haveToast = false; dirty = true; }
    if (txFlash && elapsed(now, txAt, 150)) txFlash = false;
    if (!dimmed && !pressedMask && navigation.screen() != Screen::WebSetup && prompt.kind == PairKind::None
        && elapsed(now, lastActivity, board::OLED_DIM_AFTER_MS)) {
        oled.setContrast(12); dimmed = true;
    }
    updateRgb(now);
    if (dirty && elapsed(now, lastDraw, 50)) {
        drawScreen(); lastDraw = millis(); dirty = false;
    }
    delay(1);
}
