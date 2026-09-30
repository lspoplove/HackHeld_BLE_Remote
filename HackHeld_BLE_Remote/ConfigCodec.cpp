#include "ConfigCodec.h"
#include <string.h>
namespace remote {
uint32_t configCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
bool parseUnsigned(const char* text, uint32_t maximum, uint32_t& out) {
    if (!text || !*text) return false;
    uint32_t value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(*p - '0');
        if (digit > maximum || value > (maximum - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    out = value; return true;
}
bool encodeConfig(const RemoteConfig& c, uint8_t* bytes, size_t length) {
    if (!bytes || length != CONFIG_BYTES || !validConfig(c)) return false;
    memset(bytes, 0, length);
    memcpy(bytes, "HHR3", 4); bytes[4] = 3; bytes[5] = ProfileCount; bytes[6] = ButtonCount;
    size_t at = 8;
    for (const auto& p : c.profiles) for (const auto& a : p.actions) {
        bytes[at++] = a.usage & 0xFF; bytes[at++] = a.usage >> 8;
        bytes[at++] = a.kind; bytes[at++] = a.modifiers; bytes[at++] = a.repeat;
        memcpy(bytes + at, a.label, strlen(a.label)); at += sizeof(a.label);
    }
    const uint32_t crc = configCrc32(bytes, at);
    for (unsigned i = 0; i < 4; ++i) bytes[at+i] = (crc >> (8*i)) & 0xFF;
    return true;
}
bool decodeConfig(const uint8_t* bytes, size_t length, RemoteConfig& out) {
    if (!bytes || length != CONFIG_BYTES || memcmp(bytes, "HHR3", 4) || bytes[4] != 3
        || bytes[5] != ProfileCount || bytes[6] != ButtonCount || bytes[7] != 0) return false;
    uint32_t stored = 0;
    for (unsigned i = 0; i < 4; ++i) stored |= static_cast<uint32_t>(bytes[length-4+i]) << (8*i);
    if (stored != configCrc32(bytes, length-4)) return false;
    RemoteConfig c{}; size_t at = 8;
    for (auto& p : c.profiles) for (auto& a : p.actions) {
        a.usage = bytes[at] | (static_cast<uint16_t>(bytes[at+1]) << 8); at += 2;
        a.kind = bytes[at++]; a.modifiers = bytes[at++]; a.repeat = bytes[at++];
        memcpy(a.label, bytes+at, sizeof(a.label)); at += sizeof(a.label);
    }
    if (!validConfig(c)) return false;
    out = c; return true;
}
}
