#include "BleRemote.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_random.h>
#include <atomic>
#include <stdio.h>
#include <string.h>

#if !defined(NIMBLE_CPP_VERSION_MAJOR) || NIMBLE_CPP_VERSION_MAJOR != 2 || NIMBLE_CPP_VERSION_MINOR < 5
#error "Install NimBLE-Arduino 2.5.1. This project uses the NimBLE 2.5 callback API."
#endif

namespace remote {
namespace {
constexpr uint16_t NO_CONNECTION = 0xFFFF;
constexpr uint8_t KEY_REPORT = 1, MEDIA_REPORT = 2, BOOT_REPORT = 4;
constexpr uint32_t PULSE_MS = 55, GAP_MS = 25, RELEASE_TIMEOUT_MS = 350;
constexpr uint8_t QUEUE_SIZE = 12;

// Keyboard: report 1 = modifiers, reserved, six HID usages (8 bytes).
// Consumer: report 2 = one 16-bit Consumer Usage (2 bytes).
// GATT report characteristic payloads do NOT include the report ID byte.
uint8_t reportMap[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
    0x95,0x01, 0x75,0x03, 0x91,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x26,0xE7,0x00,
    0x05,0x07, 0x19,0x00, 0x29,0xE7, 0x81,0x00, 0xC0,
    0x05,0x0C, 0x09,0x01, 0xA1,0x01, 0x85,0x02,
    0x15,0x00, 0x26,0xFF,0x03, 0x19,0x00, 0x2A,0xFF,0x03,
    0x75,0x10, 0x95,0x01, 0x81,0x00, 0xC0
};

NimBLEServer* server = nullptr;
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* keyboardChr = nullptr;
NimBLECharacteristic* mediaChr = nullptr;
NimBLECharacteristic* bootChr = nullptr;
std::atomic<uint16_t> connection{NO_CONNECTION};
std::atomic<bool> secure{false}, suspended{false}, restartWanted{false};
std::atomic<uint8_t> subscriptions{0}, protocol{1}, pairKind{0};
std::atomic<uint32_t> generation{0}, pairPin{0}, pairStarted{0};
std::atomic<uint16_t> pairHandle{NO_CONNECTION};
uint32_t observedGeneration = 0, advertiseAt = 0;
bool advertisePending = false;
uint8_t syncedMask = 0;
bool initialized = false;
std::atomic<bool> enabled{false}, closing{false};
uint32_t closeAt = 0, closeTryAt = 0;

Action queue[QUEUE_SIZE]{};
uint8_t qHead = 0, qTail = 0, qCount = 0;
Action active{};
enum class Phase : uint8_t { Idle, Down, Release, Gap };
Phase phase = Phase::Idle;
uint32_t phaseAt = 0, releaseAt = 0, releaseTryAt = 0;
uint8_t activeChannel = 0;
int eventCode = 0;
char eventLabel[13]{};

void postEvent(int code, const char* label) {
    eventCode = code;
    snprintf(eventLabel, sizeof(eventLabel), "%s", label ? label : "");
}
void publishPair(PairKind kind, uint16_t handle, uint32_t pin) {
    pairHandle.store(handle); pairPin.store(pin); pairStarted.store(millis());
    pairKind.store(static_cast<uint8_t>(kind)); // publish fields last
}
bool isCurrent(const NimBLEConnInfo& info) { return info.getConnHandle() == connection.load(); }

class ServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        if (!enabled.load() || closing.load()) {
            s->disconnect(info.getConnHandle()); return;
        }
        uint16_t expected = NO_CONNECTION;
        if (!connection.compare_exchange_strong(expected, info.getConnHandle())) {
            s->disconnect(info.getConnHandle()); return; // one active host only
        }
        secure.store(false); subscriptions.store(0); protocol.store(1); suspended.store(false);
        pairKind.store(0); generation.fetch_add(1);
        if (hid) hid->getProtocolMode()->setValue(static_cast<uint8_t>(1));
        s->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
        NimBLEDevice::startSecurity(info.getConnHandle());
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
        if (!isCurrent(info)) return;
        secure.store(false); subscriptions.store(0); pairKind.store(0);
        connection.store(NO_CONNECTION); generation.fetch_add(1); restartWanted.store(enabled.load());
    }
    uint32_t onPassKeyDisplay() override {
        const uint32_t pin = esp_random() % 1000000u;
        publishPair(PairKind::Display, connection.load(), pin);
        return pin;
    }
    void onConfirmPassKey(NimBLEConnInfo& info, uint32_t pin) override {
        if (isCurrent(info)) publishPair(PairKind::Compare, info.getConnHandle(), pin);
    }
    void onPassKeyEntry(NimBLEConnInfo& info) override {
        if (isCurrent(info)) publishPair(PairKind::Enter, info.getConnHandle(), 0);
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        if (!isCurrent(info)) return;
        pairKind.store(0);
        // Do not silently fall back to unauthenticated 'Just Works'.
        if (!info.isEncrypted() || !info.isAuthenticated()) {
            secure.store(false); server->disconnect(info.getConnHandle()); return;
        }
        secure.store(true);
    }
} serverCallbacks;

class ReportCallbacks final : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& info, uint16_t value) override {
        if (!isCurrent(info)) return;
        const uint8_t bit = chr == keyboardChr ? KEY_REPORT : (chr == mediaChr ? MEDIA_REPORT : BOOT_REPORT);
        if (value & 1) subscriptions.fetch_or(bit);
        else subscriptions.fetch_and(static_cast<uint8_t>(~bit));
    }
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
        if (!isCurrent(info)) return;
        auto value = chr->getValue();
        if (value.size() != 1) return;
        if (chr == hid->getProtocolMode()) protocol.store(value.data()[0] == 0 ? 0 : 1);
        else if (chr == hid->getHidControl()) suspended.store(value.data()[0] == 0);
    }
} reportCallbacks;

NimBLECharacteristic* characteristic(uint8_t channel) {
    if (channel == KEY_REPORT) return keyboardChr;
    if (channel == MEDIA_REPORT) return mediaChr;
    if (channel == BOOT_REPORT) return bootChr;
    return nullptr;
}
uint8_t route(const Action& a) {
    if (a.kind == Keyboard) return protocol.load() ? KEY_REPORT : BOOT_REPORT;
    if (a.kind == Consumer && protocol.load()) return MEDIA_REPORT;
    return 0;
}
bool canSend(uint8_t channel) {
    return channel && connection.load() != NO_CONNECTION && secure.load()
        && (subscriptions.load() & channel);
}
bool send(uint8_t channel, const Action* a) {
    const uint16_t handle = connection.load();
    if (!canSend(channel)) return false;
    NimBLECharacteristic* chr = characteristic(channel);
    if (!chr) return false;
    uint8_t bytes[8]{};
    const size_t length = channel == MEDIA_REPORT ? 2 : 8;
    if (a && channel == MEDIA_REPORT) {
        bytes[0] = a->usage & 0xFF; bytes[1] = a->usage >> 8;
    } else if (a) { bytes[0] = a->modifiers; bytes[2] = a->usage & 0xFF; }
    chr->setValue(bytes, length);
    return chr->notify(handle);
}
void clearQueue() { qHead = qTail = qCount = 0; }
void beginRelease(uint32_t now) {
    phase = Phase::Release; releaseAt = now; releaseTryAt = now - 15;
}
}

bool bleBegin(const char* name) {
    if (initialized) return true;
    if (!NimBLEDevice::init(name)) return false;
    NimBLEDevice::setSecurityAuth(true, true, true); // bond, MITM, Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    server = NimBLEDevice::createServer();
    if (!server) return false;
    server->setCallbacks(&serverCallbacks, false);
    server->advertiseOnDisconnect(false); // restart from the Arduino loop
    hid = new NimBLEHIDDevice(server);
    if (!hid) return false;
    hid->setManufacturer("DSTIKE");
    // Development-only identifiers. Replace with legitimately assigned IDs
    // before a commercial/certified release; do not borrow another vendor's ID.
    hid->setPnp(0x02, 0x0000, 0x0000, 0x0100);
    hid->setHidInfo(0, 0x02);
    hid->setReportMap(reportMap, sizeof(reportMap));
    keyboardChr = hid->getInputReport(1);
    mediaChr = hid->getInputReport(2);
    bootChr = hid->getBootInput();
    if (!keyboardChr || !mediaChr || !bootChr) return false;
    keyboardChr->setCallbacks(&reportCallbacks);
    mediaChr->setCallbacks(&reportCallbacks);
    bootChr->setCallbacks(&reportCallbacks);
    hid->getProtocolMode()->setCallbacks(&reportCallbacks);
    hid->getHidControl()->setCallbacks(&reportCallbacks);
    auto* output = hid->getOutputReport(1);
    auto* bootOutput = hid->getBootOutput();
    if (!output || !bootOutput) return false;
    uint8_t zeros[8]{};
    keyboardChr->setValue(zeros, 8); mediaChr->setValue(zeros, 2); bootChr->setValue(zeros, 8);
    output->setValue(zeros, 1); bootOutput->setValue(zeros, 1);
    // The hardware reference does not specify a battery-sense circuit.
    // Hide the helper's Battery Service instead of publishing a fabricated %.
    // This is a development firmware, not a Bluetooth profile qualification.
    server->removeService(hid->getBatteryService(), false);
    if (!server->start()) return false;
    auto* ad = NimBLEDevice::getAdvertising();
    ad->enableScanResponse(true);
    if (!ad->setName(name) || !ad->setAppearance(0x03C1)
        || !ad->addServiceUUID(hid->getHidService()->getUUID())) return false;
    // Advertising begins only after a Phone / Computer selection.
    initialized = true;
    return true;
}

void bleSetEnabled(bool value) {
    if (!initialized) return;
    const bool before = enabled.exchange(value);
    if (before == value) return;
    bleCancel(); eventCode = 0;
    if (!value) {
        NimBLEDevice::stopAdvertising();
        advertisePending = false; restartWanted.store(false); pairKind.store(0);
        closing.store(bleConnected());
        closeAt = millis(); closeTryAt = closeAt - 100;
    } else if (!closing.load() && !bleConnected()) {
        advertisePending = true; advertiseAt = millis() - 200;
    }
}

bool bleConnected() { return connection.load() != NO_CONNECTION; }
bool bleReady() {
    const uint8_t allowed = protocol.load() ? (KEY_REPORT | MEDIA_REPORT) : BOOT_REPORT;
    return initialized && enabled.load() && !closing.load() && bleConnected()
        && secure.load() && !suspended.load() && (subscriptions.load() & allowed);
}
const char* bleStatus() {
    if (!initialized || !enabled.load()) return "OFF";
    if (closing.load()) return "BUSY";
    if (!bleConnected()) return "WAIT";
    if (pairKind.load()) return "PAIR";
    return bleReady() ? "READY" : "LINK";
}
PairPrompt blePairPrompt() {
    return {static_cast<PairKind>(pairKind.load()), pairPin.load(), pairStarted.load()};
}
void blePairAnswer(bool accept, uint32_t enteredPin) {
    if (!server) return;
    const auto kind = static_cast<PairKind>(pairKind.load());
    const uint16_t handle = pairHandle.load();
    if (handle == NO_CONNECTION || handle != connection.load() || kind == PairKind::None) return;
    if (!accept) {
        pairKind.store(0);
        if (kind == PairKind::Compare) {
            auto info = server->getPeerInfoByHandle(handle);
            NimBLEDevice::injectConfirmPasskey(info, false);
        }
        server->disconnect(handle); return;
    }
    // Update UI before injecting: the authentication callback may finish immediately.
    pairKind.store(static_cast<uint8_t>(PairKind::Waiting));
    auto info = server->getPeerInfoByHandle(handle);
    bool ok = false;
    if (kind == PairKind::Compare) ok = NimBLEDevice::injectConfirmPasskey(info, true);
    else if (kind == PairKind::Enter && enteredPin < 1000000u)
        ok = NimBLEDevice::injectPassKey(info, enteredPin);
    if (!ok) { pairKind.store(0); server->disconnect(handle); }
}
TxResult bleQueue(const Action& action) {
    if (action.kind == Disabled) return TxResult::Disabled;
    if (!validAction(action) || !bleReady() || !canSend(route(action))
        || !(syncedMask & route(action))) return TxResult::NotReady;
    if (qCount >= QUEUE_SIZE) return TxResult::QueueFull;
    queue[qTail] = action; qTail = (qTail + 1) % QUEUE_SIZE; ++qCount;
    return TxResult::Queued;
}
void bleCancel() {
    clearQueue();
    if (phase == Phase::Down) beginRelease(millis());
    else if (phase == Phase::Gap) phase = Phase::Idle;
}
int bleTakeEvent(char* label, size_t capacity) {
    int value = eventCode; eventCode = 0;
    if (capacity) snprintf(label, capacity, "%s", eventLabel);
    return value;
}

void bleTick(uint32_t now) {
    if (!initialized) return;
    const uint32_t currentGeneration = generation.load();
    if (currentGeneration != observedGeneration) {
        observedGeneration = currentGeneration;
        clearQueue(); phase = Phase::Idle; syncedMask = 0;
    }
    if (pairKind.load() && elapsed(now, pairStarted.load(), 60000)) blePairAnswer(false);
    if (restartWanted.exchange(false)) { advertiseAt = now; advertisePending = true; }

    if (!enabled.load() && bleConnected() && !closing.exchange(true)) {
        closeAt = now; closeTryAt = now - 100;
    }
    if (closing.load()) {
        clearQueue(); phase = Phase::Idle;
        if (bleConnected()) {
            // Submit neutral reports before disconnecting the previous host.
            // A short grace period gives notifications a chance to leave the controller;
            // notify success still is NOT confirmation from the host application.
            if (!elapsed(now, closeAt, 80)) {
                if (elapsed(now, closeTryAt, 15)) {
                    closeTryAt = now;
                    for (uint8_t channel : {KEY_REPORT, MEDIA_REPORT, BOOT_REPORT})
                        if (canSend(channel)) send(channel, nullptr);
                }
            } else if (elapsed(now, closeTryAt, 100)) {
                closeTryAt = now;
                server->disconnect(connection.load());
            }
            return;
        }
        closing.store(false);
        if (enabled.load()) { advertisePending = true; advertiseAt = now - 200; }
    }
    if (!enabled.load()) {
        clearQueue(); phase = Phase::Idle; advertisePending = false;
        return;
    }
    if (advertisePending && !bleConnected() && elapsed(now, advertiseAt, 200)) {
        if (NimBLEDevice::startAdvertising()) advertisePending = false;
        else advertiseAt = now; // retry after another 200 ms (wrap-safe)
    }
    // Connection/security/subscription losses must never retain offline actions.
    if (!bleReady()) {
        clearQueue();
        if (phase == Phase::Down) beginRelease(now);
        if (!bleConnected()) { phase = Phase::Idle; return; }
    }
    // Send neutral reports once after each subscription, before any new action.
    const uint8_t subscribed = subscriptions.load();
    syncedMask &= subscribed;
    for (uint8_t channel : {KEY_REPORT, MEDIA_REPORT, BOOT_REPORT}) {
        if ((subscribed & channel) && !(syncedMask & channel) && canSend(channel)) {
            if (send(channel, nullptr)) syncedMask |= channel;
        }
    }
    if (phase == Phase::Down && elapsed(now, phaseAt, PULSE_MS)) beginRelease(now);
    if (phase == Phase::Release && elapsed(now, releaseTryAt, 15)) {
        releaseTryAt = now;
        if (send(activeChannel, nullptr)) { phase = Phase::Gap; phaseAt = now; }
        else if (elapsed(now, releaseAt, RELEASE_TIMEOUT_MS)) {
            // Failing to release is more dangerous than disconnecting a keyboard.
            clearQueue(); phase = Phase::Idle; postEvent(-1, "Release fail");
            if (bleConnected()) server->disconnect(connection.load());
        }
    }
    if (phase == Phase::Gap && elapsed(now, phaseAt, GAP_MS)) phase = Phase::Idle;
    if (phase == Phase::Idle && qCount && bleReady()) {
        active = queue[qHead]; qHead = (qHead + 1) % QUEUE_SIZE; --qCount;
        activeChannel = route(active);
        if (!(syncedMask & activeChannel) || !send(activeChannel, &active)) {
            clearQueue(); postEvent(-1, "Send failed");
            beginRelease(now); // also clear a value that may have been set before notify failed
            return;
        }
        phase = Phase::Down; phaseAt = now;
        postEvent(1, active.label);
    }
}
}
