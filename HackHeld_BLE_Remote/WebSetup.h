#pragma once
#include <stdint.h>
namespace remote {
// Called from loop only, after BLE has disconnected. No AP in normal use.
bool webBegin(uint16_t deviceId);
void webTick(uint32_t now);
void webStop();
bool webActive();
bool webExitRequested();
const char* webSsid();
const char* webPassword();
}
