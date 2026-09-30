#include "WebSetup.h"
#include "ConfigStore.h"
#include "ConfigCodec.h"
#include "WebAssets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

namespace remote {
namespace {
WebServer http(80);
bool active = false, routesAdded = false, exitRequested = false, closing = false;
uint32_t lastRequest = 0, closeAt = 0;
constexpr uint32_t IDLE_TIMEOUT_MS = 10u * 60u * 1000u;
char ssid[32]{}, password[13]{}, token[33]{};

void responseHeaders() {
    http.sendHeader("Cache-Control", "no-store");
    http.sendHeader("X-Content-Type-Options", "nosniff");
    http.sendHeader("X-Frame-Options", "DENY");
    http.sendHeader("Referrer-Policy", "no-referrer");
    http.sendHeader("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}
String quoted(const char* text) {
    String out = "\"";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p == '\\' || *p == '"') out += '\\';
        if (*p >= 32 && *p <= 126) out += static_cast<char>(*p);
    }
    out += '"'; return out;
}
void error(int code, const char* message) {
    responseHeaders(); http.send(code, "application/json", String("{\"error\":") + quoted(message) + "}");
}
bool allowed(bool write = false) {
    if (!active || closing) { error(503, "Setup is closing. Re-enter Web Setup on the device."); return false; }
    const String host = http.hostHeader();
    if (host != "192.168.4.1" && host != "192.168.4.1:80") {
        error(403, "Open http://192.168.4.1 directly."); return false;
    }
    const String origin = http.header("Origin");
    if (origin.length() && origin != "http://192.168.4.1" && origin != "http://192.168.4.1:80") {
        error(403, "Cross-origin request rejected."); return false;
    }
    if (write) {
        if (http.header("X-Setup-Token") != token) { error(403, "Session expired. Reload this page."); return false; }
        const String type = http.header("Content-Type");
        if (!type.startsWith("application/x-www-form-urlencoded")) { error(415, "Form data required."); return false; }
        if (http.clientContentLength() > 4096) { error(413, "Request too large."); return false; }
    }
    lastRequest = millis(); return true;
}
bool singleArg(const char* name, String& out) {
    int count = 0;
    for (int i = 0; i < http.args(); ++i) if (http.argName(i) == name) ++count;
    if (count != 1) return false;
    out = http.arg(name); return true;
}
bool number(const char* name, uint32_t maximum, uint32_t& value) {
    String text;
    return singleArg(name, text) && text.length() <= 10 && parseUnsigned(text.c_str(), maximum, value);
}
bool revisionMatches() {
    uint32_t value = 0;
    if (!number("revision", UINT32_MAX, value)) { error(400, "Missing or invalid revision."); return false; }
    if (value != configRevision()) { error(409, "Configuration changed in another page. Reload before saving."); return false; }
    return true;
}
void actionJson(String& json, const Action& a) {
    json += "{\"kind\":"; json += String(a.kind);
    json += ",\"usage\":"; json += String(a.usage);
    json += ",\"modifiers\":"; json += String(a.modifiers);
    json += ",\"repeat\":"; json += String(a.repeat);
    json += ",\"label\":"; json += quoted(a.label); json += '}';
}
void sendConfig() {
    RemoteConfig c{}; currentConfig(c);
    String json; json.reserve(3200);
    json = "{\"version\":3,\"revision\":"; json += String(configRevision());
    json += ",\"token\":"; json += quoted(token);
    json += ",\"storage\":"; json += quoted(configStorageStatus());
    json += ",\"profiles\":[";
    for (uint8_t p = 0; p < ProfileCount; ++p) {
        if (p) json += ',';
        json += "{\"name\":"; json += quoted(profileNames[p]); json += ",\"actions\":[";
        for (uint8_t b = 0; b < ButtonCount; ++b) { if (b) json += ','; actionJson(json, c.profiles[p].actions[b]); }
        json += "]}";
    }
    json += "]}";
    responseHeaders(); http.send(200, "application/json", json);
}
void catalogJson(String& out, const KeyChoice* list, size_t count) {
    out += '[';
    for (size_t i = 0; i < count; ++i) {
        if (i) out += ',';
        out += "{\"usage\":"; out += String(list[i].usage); out += ",\"name\":";
        out += quoted(list[i].name); out += '}';
    }
    out += ']';
}
void getCatalog() {
    if (!allowed()) return;
    String json; json.reserve(4200); json = "{\"keyboard\":";
    catalogJson(json, keyboardChoices, keyboardChoiceCount); json += ",\"media\":";
    catalogJson(json, mediaChoices, mediaChoiceCount); json += '}';
    responseHeaders(); http.send(200, "application/json", json);
}
void saveProfile() {
    if (!allowed(true) || !revisionMatches()) return;
    uint32_t profile = 0;
    if (http.args() != 38 || !number("profile", ProfileCount-1, profile)) { error(400, "Invalid profile or field count."); return; }
    RemoteConfig c{}; currentConfig(c);
    for (uint8_t i = 0; i < ButtonCount; ++i) {
        Action a{}; uint32_t kind=0, usage=0, modifiers=0, repeat=0;
        char field[24];
        snprintf(field, sizeof(field), "b%u_kind", i);
        if (!number(field, Consumer, kind)) { error(400, "Invalid action type."); return; }
        snprintf(field, sizeof(field), "b%u_usage", i);
        if (!number(field, 0xFFFF, usage)) { error(400, "Invalid key usage."); return; }
        snprintf(field, sizeof(field), "b%u_modifiers", i);
        if (!number(field, 15, modifiers)) { error(400, "Invalid modifiers."); return; }
        snprintf(field, sizeof(field), "b%u_repeat", i);
        if (!number(field, 1, repeat)) { error(400, "Invalid repeat flag."); return; }
        snprintf(field, sizeof(field), "b%u_label", i);
        String label;
        if (!singleArg(field, label) || label.length() < 1 || label.length() > 12) { error(400, "Use a 1-12 character ASCII label."); return; }
        // Reject embedded NUL/control bytes rather than silently truncating an HTTP value.
        for (size_t j = 0; j < label.length(); ++j) {
            const auto ch = static_cast<unsigned char>(label[j]);
            if (ch < 32 || ch > 126) { error(400, "Labels must contain printable ASCII only."); return; }
        }
        snprintf(field, sizeof(field), "b%u_button", i);
        uint32_t button = 0;
        if (!number(field, ButtonCount-1, button) || button != i) { error(400, "Button order mismatch."); return; }
        a.kind = kind; a.usage = usage; a.modifiers = modifiers; a.repeat = repeat;
        memcpy(a.label, label.c_str(), label.length());
        c.profiles[profile].actions[i] = a;
    }
    if (!validConfig(c)) { error(400, "Unsupported key, modifier or repeat. A/B cannot repeat."); return; }
    if (!configSave(c)) { error(500, "Storage write/readback failed. Nothing was applied in RAM; retry or reboot and reload to inspect storage."); return; }
    sendConfig();
}
void resetProfile() {
    if (!allowed(true) || !revisionMatches()) return;
    // profile=3 means all THREE profiles; it is not an additional mode.
    uint32_t profile = 0;
    if (http.args() != 2 || !number("profile", ProfileCount, profile)) { error(400, "Invalid reset request."); return; }
    RemoteConfig c{}; currentConfig(c);
    if (profile == ProfileCount) defaultConfig(c);
    else c.profiles[profile] = getDefaultProfile(static_cast<ProfileId>(profile));
    if (!configSave(c)) { error(500, "Could not save defaults. Reload to inspect storage."); return; }
    sendConfig();
}
void addRoutes() {
    if (routesAdded) return;
    const char* headers[] = {"Origin", "X-Setup-Token", "Content-Type"};
    http.collectHeaders(headers, 3);
    http.on("/", HTTP_GET, [] { if (allowed()) { responseHeaders(); http.send_P(200, "text/html; charset=utf-8", WEB_HTML); } });
    http.on("/app.css", HTTP_GET, [] { if (allowed()) { responseHeaders(); http.send_P(200, "text/css", WEB_CSS); } });
    http.on("/app.js", HTTP_GET, [] { if (allowed()) { responseHeaders(); http.send_P(200, "application/javascript", WEB_JS); } });
    http.on("/api/config", HTTP_GET, [] { if (allowed()) sendConfig(); });
    http.on("/api/catalog", HTTP_GET, getCatalog);
    http.on("/api/save", HTTP_POST, saveProfile);
    http.on("/api/reset", HTTP_POST, resetProfile);
    http.on("/api/exit", HTTP_POST, [] {
        if (!allowed(true)) return;
        if (http.args() != 0) { error(400, "Unexpected exit fields."); return; }
        responseHeaders(); http.send(200, "application/json", "{\"ok\":true}");
        closing = true; closeAt = millis();
    });
    http.onNotFound([] { error(404, "Not found. Open http://192.168.4.1"); });
    routesAdded = true;
}
}
bool webBegin(uint16_t deviceId) {
    if (active) return true;
    exitRequested = closing = false;
    // Do not store temporary Wi-Fi credentials; keep STA/router mode disabled.
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_AP)) return false;
    const IPAddress ip(192,168,4,1), mask(255,255,255,0);
    if (!WiFi.softAPConfig(ip, ip, mask)) { WiFi.mode(WIFI_OFF); return false; }
    snprintf(ssid, sizeof(ssid), "DSTIKE-SETUP-%04X", static_cast<unsigned>(deviceId));
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (size_t i = 0; i < sizeof(password)-1; ++i) password[i] = alphabet[esp_random() % (sizeof(alphabet)-1)];
    password[sizeof(password)-1] = 0;
    for (size_t i = 0; i < 4; ++i) snprintf(token + 8*i, 9, "%08lX", static_cast<unsigned long>(esp_random()));
    if (!WiFi.softAP(ssid, password, 1, 0, 1)) { WiFi.mode(WIFI_OFF); return false; }
    addRoutes(); active = true; lastRequest = millis(); http.begin(); return true;
}
void webTick(uint32_t now) {
    if (!active || exitRequested) return;
    http.handleClient();
    // handleClient may block briefly; sample millis AFTER it to avoid wrap-like underflow.
    now = millis();
    if ((closing && elapsed(now, closeAt, 300)) || elapsed(now, lastRequest, IDLE_TIMEOUT_MS)) exitRequested = true;
}
void webStop() {
    if (active) { http.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); }
    active = exitRequested = closing = false;
    memset(password, 0, sizeof(password)); memset(token, 0, sizeof(token));
}
bool webActive() { return active; }
bool webExitRequested() { return exitRequested; }
const char* webSsid() { return ssid; }
const char* webPassword() { return password; }
}
