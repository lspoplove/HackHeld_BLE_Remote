#pragma once
#include "RemoteCore.h"

namespace remote {
enum class PairKind : uint8_t { None, Compare, Display, Enter, Waiting };
struct PairPrompt { PairKind kind; uint32_t pin; uint32_t started; };
enum class TxResult : uint8_t { Queued, Disabled, NotReady, QueueFull };

// Initialize GATT, without advertising. Call bleSetEnabled(true) after selection.
bool bleBegin(const char* name);
// false stops advertisements and releases/disconnects an existing host.
// true resumes advertisements; it does NOT identify a phone vs a computer.
void bleSetEnabled(bool enabled);
void bleTick(uint32_t now);
TxResult bleQueue(const Action& action);
void bleCancel();
bool bleConnected();
bool bleReady();
const char* bleStatus();
PairPrompt blePairPrompt();
void blePairAnswer(bool accept, uint32_t enteredPin = 0);
// Loop-thread notifications: 1 = report queued to BLE stack; -1 = send failed;
// Not host-application confirmation.
int bleTakeEvent(char* label, size_t capacity);
}
