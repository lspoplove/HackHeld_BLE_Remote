#include "WebMockRuntime.h"
#include "RemoteCore.h"
#include "ConfigStore.h"
#include "WebSetup.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace remote;
static int checks=0;
#define CHECK(condition,name) do { assert(condition); ++checks; std::printf("PASS %s\n",name); } while(0)
using Fields=std::vector<std::pair<std::string,std::string>>;
using Headers=std::map<std::string,std::string>;
std::string extractToken() {
    const std::string key="\"token\":\""; auto at=mock::http->body.find(key);
    assert(at!=std::string::npos); return mock::http->body.substr(at+key.size(),32);
}
Fields form(unsigned profile) {
    RemoteConfig c{}; currentConfig(c);
    Fields f={{"profile",std::to_string(profile)}, {"revision",std::to_string(configRevision())}};
    for(unsigned i=0;i<ButtonCount;++i) {
        auto a=c.profiles[profile].actions[i]; std::string prefix="b"+std::to_string(i)+"_";
        f.emplace_back(prefix+"kind",std::to_string(a.kind)); f.emplace_back(prefix+"usage",std::to_string(a.usage));
        f.emplace_back(prefix+"modifiers",std::to_string(a.modifiers)); f.emplace_back(prefix+"repeat",std::to_string(a.repeat));
        f.emplace_back(prefix+"label",a.label); f.emplace_back(prefix+"button",std::to_string(i));
    }
    return f;
}
void set(Fields& f,const char* key,const char* value) {for(auto& p:f) if(p.first==key){p.second=value;return;} assert(false);}
int main(int argc,char** argv) {
    configLoad();
    CHECK(!webActive()&&!mock::wifiOn,"Wi-Fi is off before Web Setup");
    CHECK(webBegin(0x1234)&&webActive()&&mock::apOn,"explicit entry starts an AP");
    CHECK(std::strlen(webPassword())==12&&std::string(webSsid())=="DSTIKE-SETUP-1234","OLED credentials and per-device SSID generated");
    mock::http->request(HTTP_GET,"/");
    CHECK(mock::http->code==200&&mock::http->body.find("Make every key yours")!=std::string::npos,"embedded HTML route responds");
    CHECK(mock::http->resHeaders["Cache-Control"]=="no-store"&&mock::http->resHeaders["X-Frame-Options"]=="DENY","no-cache and frame protection headers");
    mock::http->request(HTTP_GET,"/api/config");
    CHECK(mock::http->code==200&&mock::http->body.find("Phone Media")!=std::string::npos,"config API includes the three profiles");
    std::string token=extractToken(); CHECK(token.size()==32,"per-session write token returned");
    if(argc>1) {std::ofstream f(std::string(argv[1])+"/config.json");f<<mock::http->body;}
    mock::http->request(HTTP_GET,"/api/catalog");
    CHECK(mock::http->code==200&&mock::http->body.find("F24")!=std::string::npos,"catalog includes supported keyboard and media usages");
    if(argc>1) {std::ofstream f(std::string(argv[1])+"/catalog.json");f<<mock::http->body;}
    Headers headers={{"Origin","http://192.168.4.1"},{"X-Setup-Token",token},{"Content-Type","application/x-www-form-urlencoded"}};
    auto f=form(1); set(f,"b4_usage","6");set(f,"b4_modifiers","1");set(f,"b4_label","Copy");
    mock::http->request(HTTP_POST,"/api/save",f);
    CHECK(mock::http->code==403&&mock::nvsWrites==0,"save without session token rejected");
    auto badHeaders=headers;badHeaders["Origin"]="https://example.com";
    mock::http->request(HTTP_POST,"/api/save",f,badHeaders); CHECK(mock::http->code==403,"cross-origin write rejected");
    badHeaders=headers;badHeaders["Host"]="evil.example";
    mock::http->request(HTTP_GET,"/api/config",{},badHeaders); CHECK(mock::http->code==403,"non-device Host rejected");
    badHeaders=headers;badHeaders["Content-Type"]="application/json";
    mock::http->request(HTTP_POST,"/api/save",f,badHeaders); CHECK(mock::http->code==415,"unexpected content type rejected");
    mock::http->request(HTTP_POST,"/api/save",f,headers,5000); CHECK(mock::http->code==413,"oversized body rejected by endpoint");
    auto bad=f;bad.pop_back();mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"missing field rejected");
    bad=f;bad.back().first="b0_kind";mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"duplicate field rejected");
    bad=f;set(bad,"b4_repeat","1");mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"A-repeat rejected over HTTP");
    bad=f;set(bad,"b0_usage","-1");mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"negative usage rejected over HTTP");
    bad=f;set(bad,"b4_label","ThisLabelIsTooLong");mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"long label rejected");
    bad=f;set(bad,"b4_label","\xe4\xb8\xad");mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"non-ASCII label rejected");
    bad=f;set(bad,"b0_button","1");mock::http->request(HTTP_POST,"/api/save",bad,headers);CHECK(mock::http->code==400,"button order mismatch rejected");
    mock::http->request(HTTP_POST,"/api/save",f,headers);
    CHECK(mock::http->code==200&&getProfile(ProfileId::Presentation).actions[A].usage==6&&getProfile(ProfileId::Presentation).actions[A].modifiers==1,"valid Ctrl+C saved and applied");
    CHECK(getProfile(ProfileId::PhoneMedia).actions[A].usage==0xCD&&getProfile(ProfileId::ComputerMedia).actions[A].usage==0xCD,"saving presentation leaves other profiles intact");
    mock::http->request(HTTP_POST,"/api/save",f,headers);CHECK(mock::http->code==409,"stale browser revision rejected");
    f=form(1);set(f,"b4_label","A\"\\<>&");mock::http->request(HTTP_POST,"/api/save",f,headers);
    CHECK(mock::http->code==200&&mock::http->body.find("A\\\"\\\\<>&")!=std::string::npos,"quote and backslash safely encoded in JSON");
    f=form(1);set(f,"b4_label","NewLabel");mock::nvsWriteOk=false;mock::http->request(HTTP_POST,"/api/save",f,headers);
    CHECK(mock::http->code==500&&std::string(getProfile(ProfileId::Presentation).actions[A].label)!="NewLabel","storage failure is not presented as success");mock::nvsWriteOk=true;
    f={{"profile","1"},{"revision",std::to_string(configRevision())}};
    mock::http->request(HTTP_POST,"/api/reset",f,headers);
    CHECK(mock::http->code==200&&getProfile(ProfileId::Presentation).actions[A].usage==0x3E,"reset profile restores exact v2 default");
    f={{"profile","3"},{"revision",std::to_string(configRevision())}};
    mock::http->request(HTTP_POST,"/api/reset",f,headers); CHECK(mock::http->code==200,"restore all endpoint accepted");
    mock::http->request(HTTP_GET,"/api/save",{},headers);CHECK(mock::http->code==404,"GET cannot mutate configuration");
    mock::http->request(HTTP_POST,"/api/exit",{},headers);CHECK(mock::http->code==200,"web exit responds before closing");
    mock::now+=299;webTick(mock::now);CHECK(!webExitRequested(),"exit allows response grace period");
    ++mock::now;webTick(mock::now);CHECK(webExitRequested(),"exit event emitted after grace period");
    webStop();CHECK(!webActive()&&!mock::wifiOn&&!mock::apOn&&std::strlen(webPassword())==0,"exit stops AP and clears session secrets");
    CHECK(webBegin(0x1234),"setup can restart without duplicate routes");
    mock::now+=600000;webTick(mock::now);CHECK(webExitRequested(),"idle session expires after ten minutes");
    webStop();mock::allowAp=false;CHECK(!webBegin(0x1234)&&!mock::wifiOn&&!webActive(),"AP start failure cleans up Wi-Fi");
    std::printf("\n%d web endpoint checks passed using transport test doubles.\n",checks);
}
