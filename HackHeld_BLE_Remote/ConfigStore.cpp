#include "ConfigStore.h"
#include "ConfigCodec.h"
#include <Preferences.h>
#include <string.h>
namespace remote {
namespace {
constexpr char NAMESPACE[] = "hh-remote-v3";
constexpr char KEY[] = "keys";
uint32_t revision = 0;
bool hasSaved = false;
const char* storageStatus = "Factory defaults";
}
void configLoad() {
    RemoteConfig c{}; defaultConfig(c); applyConfig(c);
    revision = 0; hasSaved = false;
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) { storageStatus = "NVS unavailable: defaults"; return; }
    const size_t length = prefs.getBytesLength(KEY);
    if (!length) { storageStatus = "Factory defaults"; prefs.end(); return; }
    uint8_t bytes[CONFIG_BYTES];
    const bool ok = length == sizeof(bytes) && prefs.getBytes(KEY, bytes, sizeof(bytes)) == sizeof(bytes)
        && decodeConfig(bytes, sizeof(bytes), c);
    prefs.end();
    if (!ok) { storageStatus = "Invalid saved data: defaults"; return; }
    applyConfig(c); hasSaved = true; storageStatus = "Saved on device";
}
bool configSave(const RemoteConfig& c) {
    uint8_t bytes[CONFIG_BYTES], previous[CONFIG_BYTES];
    if (!encodeConfig(c, bytes, sizeof(bytes))) return false;
    RemoteConfig old{}; currentConfig(old);
    if (hasSaved && encodeConfig(old, previous, sizeof(previous)) && !memcmp(bytes, previous, sizeof(bytes)))
        return true; // no-op saves do not wear flash or change the revision
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) return false;
    const size_t written = prefs.putBytes(KEY, bytes, sizeof(bytes));
    const bool verified = written == sizeof(bytes) && prefs.getBytesLength(KEY) == sizeof(bytes)
        && prefs.getBytes(KEY, previous, sizeof(previous)) == sizeof(previous)
        && !memcmp(bytes, previous, sizeof(bytes));
    prefs.end();
    if (!verified) return false; // never claim a failed save succeeded
    applyConfig(c); ++revision; hasSaved = true; storageStatus = "Saved on device";
    return true;
}
uint32_t configRevision() { return revision; }
const char* configStorageStatus() { return storageStatus; }
}
