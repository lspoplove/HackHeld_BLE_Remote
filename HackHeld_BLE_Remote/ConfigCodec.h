#pragma once
#include "RemoteCore.h"
namespace remote {
// Explicit little-endian wire format. No native struct padding is written.
constexpr size_t CONFIG_BYTES = 8 + ProfileCount * ButtonCount * 18 + 4;
bool encodeConfig(const RemoteConfig& c, uint8_t* bytes, size_t length);
bool decodeConfig(const uint8_t* bytes, size_t length, RemoteConfig& out);
bool parseUnsigned(const char* text, uint32_t maximum, uint32_t& out);
uint32_t configCrc32(const uint8_t* data, size_t length);
}
