#pragma once
// Deliberately small HOST-ONLY test doubles. These are NOT Arduino/NimBLE
// implementations and cannot establish target compilation or real BLE behavior.
#include <stdint.h>
#include <stddef.h>
#include <array>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ESP_ARDUINO_VERSION_MAJOR 3
#define NIMBLE_CPP_VERSION_MAJOR 2
#define NIMBLE_CPP_VERSION_MINOR 5
#define ARDUINO_USB_CDC_ON_BOOT 0
#define ARDUINO_USB_MSC_ON_BOOT 0
#define ARDUINO_USB_DFU_ON_BOOT 0
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT 4
#define U8G2_R0 0
#define U8X8_PIN_NONE 255
#define LED_COLOR_ORDER_GRB 0
#define BLE_HS_IO_DISPLAY_YESNO 1
inline const uint8_t u8g2_font_5x7_tf[]={0};
inline const uint8_t u8g2_font_6x10_tf[]={0};
inline const uint8_t u8g2_font_9x15_tf[]={0};
namespace mock {
inline uint32_t now=0;
inline std::array<int,49> pins=[] { std::array<int,49> a{}; a.fill(HIGH); return a; }();
inline std::vector<std::string> frame;
inline bool advertising=false, allowNotify=true, allowAdvertising=true;
inline int initCalls=0, disconnects=0, advertStarts=0;
struct Report { int channel; std::vector<uint8_t> bytes; };
inline std::vector<Report> reports;
}
inline uint32_t millis() { return mock::now; }
inline void delay(unsigned ms) { mock::now+=ms; }
inline void pinMode(int,int) {}
inline int digitalRead(int pin) { return mock::pins.at(pin); }
inline void digitalWrite(int pin,int value) { mock::pins.at(pin)=value; }
inline bool psramFound() { return false; }
inline uint32_t esp_random() { return 123456; }
inline void rgbLedWriteOrdered(int,int,uint8_t,uint8_t,uint8_t) {}
struct SerialMock { void begin(int) {} void println(const char*) {} };
inline SerialMock Serial0;
struct EspMock { uint64_t getEfuseMac() { return 0x1234u; } };
inline EspMock ESP;
struct WireMock {
    bool begin(int,int) {return true;} void setClock(uint32_t) {} void setTimeOut(uint16_t) {}
    void beginTransmission(uint8_t) {} uint8_t endTransmission() {return 0;}
};
inline WireMock Wire;
class U8G2_SH1106_128X64_NONAME_F_HW_I2C {
public:
    U8G2_SH1106_128X64_NONAME_F_HW_I2C(int,int,int,int) {}
    bool begin() {return true;} void setI2CAddress(uint8_t) {} void setBusClock(uint32_t) {}
    void setContrast(uint8_t) {} void setFont(const uint8_t*) {}
    void drawStr(int,int,const char* s) {mock::frame.emplace_back(s);}
    void drawHLine(int,int,int) {} void drawBox(int,int,int,int) {} void drawFrame(int,int,int,int) {}
    void setDrawColor(int) {} void clearBuffer() {mock::frame.clear();} void sendBuffer() {}
};
class NimBLEConnInfo {
public:
    uint16_t handle=1; bool encrypted=false, authenticated=false;
    uint16_t getConnHandle() const {return handle;}
    bool isEncrypted() const {return encrypted;}
    bool isAuthenticated() const {return authenticated;}
};
class NimBLEServer;
class NimBLECharacteristic;
class NimBLEServerCallbacks {
public:
    virtual ~NimBLEServerCallbacks()=default;
    virtual void onConnect(NimBLEServer*,NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int) {}
    virtual uint32_t onPassKeyDisplay() {return 0;}
    virtual void onConfirmPassKey(NimBLEConnInfo&,uint32_t) {}
    virtual void onPassKeyEntry(NimBLEConnInfo&) {}
    virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
};
class NimBLECharacteristicCallbacks {
public:
    virtual ~NimBLECharacteristicCallbacks()=default;
    virtual void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo&,uint16_t) {}
    virtual void onWrite(NimBLECharacteristic*,NimBLEConnInfo&) {}
};
class NimBLEAttValue {
public:
    std::vector<uint8_t> bytes;
    size_t size() const {return bytes.size();}
    const uint8_t* data() const {return bytes.data();}
};
class NimBLECharacteristic {
public:
    int channel=0;
    NimBLECharacteristicCallbacks* cb=nullptr;
    NimBLEAttValue value;
    explicit NimBLECharacteristic(int c=0):channel(c) {}
    void setCallbacks(NimBLECharacteristicCallbacks* c) {cb=c;}
    void setValue(const uint8_t* v,size_t n) {value.bytes.assign(v,v+n);}
    void setValue(uint8_t v) {setValue(&v,1);}
    bool notify(uint16_t) {
        if (!mock::allowNotify) return false;
        mock::reports.push_back({channel,value.bytes}); return true;
    }
    NimBLEAttValue getValue() {return value;}
};
class NimBLEService {public: std::string getUUID() {return "1812";} };
class NimBLEAdvertising {
public:
    void enableScanResponse(bool) {}
    bool setName(const char*) {return true;} bool setAppearance(uint16_t) {return true;}
    bool addServiceUUID(const std::string&) {return true;}
};
class NimBLEServer {
public:
    NimBLEServerCallbacks* cb=nullptr;
    NimBLEConnInfo current;
    bool start() {return true;}
    void setCallbacks(NimBLEServerCallbacks* c,bool) {cb=c;}
    void advertiseOnDisconnect(bool) {}
    void updateConnParams(uint16_t,uint16_t,uint16_t,uint16_t,uint16_t) {}
    void removeService(NimBLEService*,bool) {}
    NimBLEConnInfo getPeerInfoByHandle(uint16_t h) {auto i=current; i.handle=h; return i;}
    bool disconnect(uint16_t h) {
        ++mock::disconnects; auto i=getPeerInfoByHandle(h);
        if (cb) cb->onDisconnect(this,i,0);
        return true;
    }
};
namespace mock {
inline NimBLEServer server;
inline NimBLEAdvertising ad;
inline NimBLECharacteristic keyboard(1),media(2),boot(4),protocol,hidControl,output,bootOutput;
inline NimBLEService hidService,batteryService;
}
class NimBLEHIDDevice {
public:
    explicit NimBLEHIDDevice(NimBLEServer*) {}
    bool setManufacturer(const std::string&) {return true;}
    void setPnp(uint8_t,uint16_t,uint16_t,uint16_t) {}
    void setHidInfo(uint8_t,uint8_t) {}
    void setReportMap(uint8_t*,uint16_t) {}
    NimBLECharacteristic* getInputReport(uint8_t id) {return id==1?&mock::keyboard:&mock::media;}
    NimBLECharacteristic* getBootInput() {return &mock::boot;}
    NimBLECharacteristic* getProtocolMode() {return &mock::protocol;}
    NimBLECharacteristic* getHidControl() {return &mock::hidControl;}
    NimBLECharacteristic* getOutputReport(uint8_t) {return &mock::output;}
    NimBLECharacteristic* getBootOutput() {return &mock::bootOutput;}
    NimBLEService* getBatteryService() {return &mock::batteryService;}
    NimBLEService* getHidService() {return &mock::hidService;}
};
class NimBLEDevice {
public:
    static bool init(const char*) {++mock::initCalls; return true;}
    static void setSecurityAuth(bool,bool,bool) {}
    static void setSecurityIOCap(uint8_t) {}
    static NimBLEServer* createServer() {return &mock::server;}
    static bool startSecurity(uint16_t) {return true;}
    static bool injectConfirmPasskey(const NimBLEConnInfo&,bool) {return true;}
    static bool injectPassKey(const NimBLEConnInfo&,uint32_t) {return true;}
    static NimBLEAdvertising* getAdvertising() {return &mock::ad;}
    static bool stopAdvertising() {mock::advertising=false; return true;}
    static bool startAdvertising() {
        ++mock::advertStarts;
        if (!mock::allowAdvertising) return false;
        mock::advertising=true; return true;
    }
};
namespace mock {
inline void connect() {
    advertising=false; server.current={1,false,false};
    server.cb->onConnect(&server,server.current);
}
inline void authenticate() {
    server.current.encrypted=server.current.authenticated=true;
    server.cb->onAuthenticationComplete(server.current);
}
inline void subscribe() {
    keyboard.cb->onSubscribe(&keyboard,server.current,1);
    media.cb->onSubscribe(&media,server.current,1);
}
}
