#pragma once
// Host test doubles ONLY. No Wi-Fi, flash, real HTTP parser or TLS runs here.
#include "MockRuntime.h"
#include <functional>
#include <type_traits>
#include <cstring>
#ifndef PROGMEM
#define PROGMEM
#endif
class String {
    std::string value;
public:
    String() = default;
    String(const char* s): value(s ? s : "") {}
    String(const std::string& s): value(s) {}
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    String(T n): value(std::to_string(n)) {}
    const char* c_str() const { return value.c_str(); }
    size_t length() const { return value.size(); }
    bool reserve(size_t n) { value.reserve(n); return true; }
    bool startsWith(const char* p) const { return value.rfind(p, 0) == 0; }
    char operator[](size_t i) const { return value[i]; }
    String& operator+=(const String& s) { value += s.value; return *this; }
    String& operator+=(const char* s) { value += s; return *this; }
    String& operator+=(char c) { value += c; return *this; }
    friend String operator+(String a, const String& b) { a += b; return a; }
    friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};
namespace mock {
inline std::map<std::string, std::vector<uint8_t>> nvs;
inline bool nvsOpenOk = true, nvsWriteOk = true;
inline int nvsWrites = 0;
inline bool wifiOn = false, apOn = false, allowAp = true;
inline std::string ssid, wifiPassword;
}
class Preferences {
    std::string space;
public:
    bool begin(const char* name, bool) { space = name; return mock::nvsOpenOk; }
    void end() {}
    size_t getBytesLength(const char* name) { return mock::nvs[space + "/" + name].size(); }
    size_t getBytes(const char* name, void* dst, size_t cap) {
        auto& v = mock::nvs[space + "/" + name];
        if (v.size() > cap) return 0;
        std::memcpy(dst, v.data(), v.size()); return v.size();
    }
    size_t putBytes(const char* name, const void* src, size_t length) {
        if (!mock::nvsWriteOk) return 0;
        const auto* p = static_cast<const uint8_t*>(src);
        mock::nvs[space + "/" + name].assign(p, p+length); ++mock::nvsWrites; return length;
    }
};
struct IPAddress { IPAddress(int,int,int,int) {} };
constexpr int WIFI_OFF = 0, WIFI_AP = 2;
class WiFiMock {
public:
    void persistent(bool) {}
    bool mode(int m) { mock::wifiOn = m != WIFI_OFF; if (!mock::wifiOn) mock::apOn = false; return true; }
    bool softAPConfig(IPAddress,IPAddress,IPAddress) { return true; }
    bool softAP(const char* s, const char* p, int, int, int count) {
        if (count != 1 || !mock::allowAp) return false;
        mock::ssid = s; mock::wifiPassword = p; mock::apOn = true; return true;
    }
    bool softAPdisconnect(bool) { mock::apOn = false; return true; }
};
inline WiFiMock WiFi;
enum HTTPMethod { HTTP_GET = 0, HTTP_POST = 1, HTTP_OPTIONS = 2 };
class WebServer;
namespace mock { inline WebServer* http = nullptr; }
class WebServer {
    using Handler = std::function<void()>;
    std::map<std::pair<std::string,int>, Handler> routes;
    Handler missing;
public:
    std::vector<std::pair<std::string,std::string>> fields;
    std::map<std::string,std::string> reqHeaders, resHeaders;
    int code = 0, contentLength = 0;
    bool running = false;
    std::string body, mime;
    explicit WebServer(int) { mock::http = this; }
    void collectHeaders(const char*[],size_t) {}
    void on(const char* path, HTTPMethod method, Handler fn) { routes[{path, method}] = std::move(fn); }
    void onNotFound(Handler fn) { missing = std::move(fn); }
    void begin() { running = true; }
    void stop() { running = false; }
    void handleClient() {}
    String hostHeader() const { return header("Host"); }
    String header(const String& name) const { auto i = reqHeaders.find(name.c_str()); return i == reqHeaders.end() ? String() : String(i->second); }
    int clientContentLength() const { return contentLength; }
    int args() const { return static_cast<int>(fields.size()); }
    String argName(int i) const { return fields.at(i).first; }
    String arg(const String& name) const { for (auto& p : fields) if (String(p.first) == name) return p.second; return {}; }
    void sendHeader(const String& name, const String& value) { resHeaders[name.c_str()] = value.c_str(); }
    void send(int c, const char* type, const String& data) { code=c; mime=type; body=data.c_str(); }
    void send_P(int c, const char* type, const char* data) { send(c,type,String(data)); }
    void request(HTTPMethod method, const char* path,
                 const std::vector<std::pair<std::string,std::string>>& input = {},
                 const std::map<std::string,std::string>& headers = {}, int length = 1024) {
        fields=input; reqHeaders=headers; resHeaders.clear(); body.clear(); code=0; contentLength=length;
        if (!reqHeaders.count("Host")) reqHeaders["Host"]="192.168.4.1";
        if (!running) { code=503; return; }
        auto it=routes.find({path, method});
        if (it!=routes.end()) it->second(); else if(missing) missing();
    }
};
