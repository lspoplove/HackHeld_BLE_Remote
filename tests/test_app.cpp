// Runs the actual App.cpp + BleRemote.cpp against mocked hardware/transport.
// This tests integration logic, NOT ESP32 compilation or over-the-air behavior.
#include "WebMockRuntime.h"
#include "WebSetup.h"
#include "ConfigStore.h"
#include "App.h"
#include "BoardConfig.h"
#include "RemoteCore.h"
#include "BleRemote.h"
#include <cassert>
#include <cstdio>
using namespace remote;
constexpr uint8_t bit(Button b) {return 1u<<b;}
static int checks=0;
void check(bool ok,const char* message) {if(!ok) {std::fprintf(stderr,"FAIL: %s\n",message); std::abort();} ++checks; std::printf("PASS %s\n",message);}
void run(unsigned ms) {uint32_t until=mock::now+ms; while(mock::now<until) appLoop();}
void mask(uint8_t bits) {for(int i=0;i<ButtonCount;++i) mock::pins[board::BUTTON_PINS[i]]=(bits&(1u<<i))?LOW:HIGH;}
void tap(Button b) {mask(bit(b));run(60);mask(0);run(100);}
void back() {mask(bit(A)|bit(B));run(1280);mask(0);run(250);}
bool screenHas(const char* text) {return std::find(mock::frame.begin(),mock::frame.end(),text)!=mock::frame.end();}
std::vector<unsigned> usages(int channel) {
    std::vector<unsigned> out;
    for(const auto& r:mock::reports) {
        if(r.channel!=channel) continue;
        unsigned usage=channel==2?(r.bytes[0]|(r.bytes[1]<<8)):r.bytes[2];
        if(usage) out.push_back(usage);
    }
    return out;
}
void ready() {mock::connect();mock::authenticate();mock::subscribe();run(250);mock::reports.clear();}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    appSetup();run(50);
#ifdef TEST_UNVERIFIED
    check(screenHas("PIN MAP CHECK"),"default pin-map gate stays closed");
    check(mock::initCalls==0,"unverified hardware never initializes BLE");
    check(!mock::wifiOn&&!webActive(),"unverified hardware never enables Wi-Fi");
    return 0;
#else
    check(screenHas("CONNECT TO")&&screenHas("Phone")&&screenHas("Computer"),"boot preserves Phone / Computer choices");
    check(mock::initCalls==0&&!mock::advertising,"no BLE initialization/advertising before selection");
    tap(A);
    check(screenHas("Phone Media")&&mock::advertising,"Phone starts advertising and opens phone controls");
    check(mock::initCalls==1,"BLE initializes once after selection");
    ready();check(bleReady(),"simulated encrypted subscribed link is ready");
    for(int i=0;i<ButtonCount;++i) tap(static_cast<Button>(i));
    check(usages(2)==std::vector<unsigned>({0xB6,0xB5,0xE9,0xEA,0xCD,0xE2}),"all six phone buttons submit correct media usages");
    check(usages(1).empty(),"phone controls submit no keyboard usages");
    mock::reports.clear();back();
    check(screenHas("CONNECT TO")&&!mock::advertising&&!bleConnected(),"Phone A+B returns to device menu and disconnects");
    check(usages(1).empty()&&usages(2).empty(),"exit chord submits no play/pause or mute");
    tap(Down);tap(A);
    check(screenHas("COMPUTER")&&screenHas("Presentation")&&screenHas("Media"),"Computer has only two modes");
    check(mock::initCalls==1,"returning to selection reuses BLE rather than reinitializing it");
    ready();tap(A);
    check(screenHas("Presentation")&&usages(1).empty()&&usages(2).empty(),"confirm/release does not leak an F5 or media action");
    for(int i=0;i<ButtonCount;++i) tap(static_cast<Button>(i));
    check(usages(1)==std::vector<unsigned>({0x4B,0x4E,0x4A,0x4D,0x3E,0x29}),"all six Presentation buttons preserve v1 mapping");
    check(usages(2).empty(),"Presentation submits no media usages");
    const int disconnects=mock::disconnects;mock::reports.clear();back();
    check(screenHas("COMPUTER")&&bleConnected()&&disconnects==mock::disconnects,"PC A+B returns to modes without disconnecting");
    check(usages(1).empty()&&usages(2).empty(),"PC exit chord submits no F5/Esc");
    tap(Down);tap(A);
    check(screenHas("PC Media"),"PC Media opens from the two-item submenu");
    for(int i=0;i<ButtonCount;++i) tap(static_cast<Button>(i));
    check(usages(2)==std::vector<unsigned>({0xE9,0xEA,0xB6,0xB5,0xCD,0xE2}),"all six PC Media buttons preserve v1 mapping");
    check(usages(1).empty(),"PC Media submits no keyboard usages");
    mock::reports.clear();back();tap(B);run(250);
    check(screenHas("CONNECT TO")&&!bleConnected()&&!mock::advertising,"PC menu B returns to device selection and disconnects");
    check(usages(1).empty()&&usages(2).empty(),"menu-only navigation never submits host commands");
    // Offline taps and held keys must not be replayed after reconnection.
    tap(Up);tap(A); // device menu currently highlights Computer; move to Phone
    mock::reports.clear();tap(A);
    mock::connect();mock::authenticate();mock::subscribe();run(250);
    check(usages(1).empty()&&usages(2).empty(),"offline taps are not replayed on reconnection");
    mock::reports.clear();mask(bit(A));run(60);
    mock::server.disconnect(1);run(250);
    mock::connect();mock::authenticate();mock::subscribe();run(250);
    mask(0);run(150);
    check(usages(1).empty()&&usages(2).empty(),"A held across reconnect does not fire on release");
    tap(A);
    check(usages(2)==std::vector<unsigned>({0xCD}),"fresh A tap still works after reconnect guard");
    mock::reports.clear();mock::server.disconnect(1);run(250);
    mock::connect();mock::server.cb->onConfirmPassKey(mock::server.current,123456);run(100);
    tap(A);
    check(blePairPrompt().kind==PairKind::Waiting&&usages(2).empty(),"pairing A confirmation is not a play/pause command");
    mock::authenticate();mock::subscribe();run(250);
    check(bleReady()&&usages(2).empty(),"finishing pairing does not replay the confirmation key");
    // Test Web Setup through the real application navigation.
    back(); tap(Up); tap(A); run(150);
    check(screenHas("WEB SETUP")&&webActive()&&mock::apOn,"root Web Setup starts AP and shows credentials");
    check(!bleConnected()&&!mock::advertising,"Web Setup runs without an active BLE host or advertising");
    check(screenHas("http://192.168.4.1"),"OLED shows the manual browser address");
    mock::reports.clear(); tap(Up); tap(Down); tap(Left); tap(Right); tap(A);
    check(usages(1).empty()&&usages(2).empty(),"physical keys in Web Setup never submit HID commands");
    RemoteConfig custom{};currentConfig(custom);custom.profiles[0].actions[A].usage=0xB5;
    std::strcpy(custom.profiles[0].actions[A].label,"Next A");
    custom.profiles[1].actions[Left]={6,Keyboard,3,0,"New left"};
    check(configSave(custom),"editable mappings saved while setup is open");
    tap(B);
    check(screenHas("CONNECT TO")&&!webActive()&&!mock::wifiOn,"B exits Web Setup and stops Wi-Fi");
    tap(Down);tap(A);ready();tap(A);
    check(usages(2)==std::vector<unsigned>({0xB5}),"Phone uses the saved A mapping after setup exit");
    check(screenHas("A:Next A"),"OLED uses the configured action label");
    back();tap(Down);tap(A);ready();tap(A);tap(Left);
    check(usages(1)==std::vector<unsigned>({6}),"Presentation uses the configured keyboard key");
    bool modifierFound=false;
    for(const auto& report:mock::reports) if(report.channel==1&&report.bytes[2]==6&&report.bytes[0]==3) modifierFound=true;
    check(modifierFound,"configured Ctrl+Shift modifiers reach the keyboard report");
    check(screenHas("L:New left"),"Presentation left label is not overwritten by old hard-coded text");
    back();tap(B);tap(Down);tap(A);run(150);
    check(webActive(),"Web Setup can be entered again after BLE use");
    mock::http->request(HTTP_GET,"/api/config");
    const std::string tokenKey="\"token\":\"";
    const auto tokenAt=mock::http->body.find(tokenKey);
    check(tokenAt!=std::string::npos,"web API available from the integrated application");
    const auto token=mock::http->body.substr(tokenAt+tokenKey.size(),32);
    mock::http->request(HTTP_POST,"/api/exit",{},{{"X-Setup-Token",token},{"Content-Type","application/x-www-form-urlencoded"}});
    run(400);
    check(screenHas("CONNECT TO")&&!webActive()&&!mock::wifiOn,"browser exit returns application to root and closes Wi-Fi");
    tap(A);run(150);mock::now+=600001;run(100);
    check(screenHas("CONNECT TO")&&!webActive(),"idle web timeout returns application to root");
    std::printf("\n%d mocked integration checks passed; NOT physical BLE tests.\n",checks);
#endif
}
