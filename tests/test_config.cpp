#include "ConfigCodec.h"
#include "ConfigStore.h"
#include "WebMockRuntime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace remote;
static int checks = 0;
#define CHECK(condition, name) do { assert(condition); ++checks; std::printf("PASS %s\n", name); } while(0)
bool equal(const RemoteConfig& a, const RemoteConfig& b) {
    uint8_t x[CONFIG_BYTES], y[CONFIG_BYTES];
    return encodeConfig(a,x,sizeof(x)) && encodeConfig(b,y,sizeof(y)) && !std::memcmp(x,y,sizeof(x));
}
int main() {
    RemoteConfig defaults{}, changed{}, decoded{}; defaultConfig(defaults); changed=defaults;
    CHECK(validConfig(defaults), "all factory defaults are valid");
    uint8_t data[CONFIG_BYTES];
    CHECK(encodeConfig(defaults,data,sizeof(data)), "explicit wire-format encoding");
    CHECK(decodeConfig(data,sizeof(data),decoded) && equal(defaults,decoded), "wire-format round trip");
    data[20] ^= 1;
    CHECK(!decodeConfig(data,sizeof(data),decoded), "CRC rejects a corrupted record");
    CHECK(equal(decoded, defaults), "failed decode does not modify output");
    CHECK(!decodeConfig(data,sizeof(data)-1,decoded), "truncated record rejected");
    encodeConfig(defaults,data,sizeof(data)); data[4]=4;
    CHECK(!decodeConfig(data,sizeof(data),decoded), "unknown schema rejected");
    uint32_t value=99;
    CHECK(parseUnsigned("4294967295",UINT32_MAX,value)&&value==UINT32_MAX, "maximum unsigned integer parses");
    CHECK(!parseUnsigned("4294967296",UINT32_MAX,value), "integer overflow rejected");
    CHECK(!parseUnsigned("-1",65535,value)&&!parseUnsigned("1x",65535,value)&&!parseUnsigned("",65535,value), "negative malformed and empty integers rejected");
    CHECK(!parseUnsigned(" 1",65535,value)&&!parseUnsigned("16",15,value)&&!parseUnsigned("1",0,value), "whitespace and bounded overflow rejected");
    auto& action=changed.profiles[1].actions[A];
    action.kind=Keyboard; action.usage=0x06; action.modifiers=1; std::strcpy(action.label,"Copy");
    CHECK(validConfig(changed), "Ctrl+C mapping accepted");
    action.repeat=1; CHECK(!validConfig(changed), "A-repeat is rejected by config validator"); action.repeat=0;
    action.modifiers=16; CHECK(!validConfig(changed), "unsupported modifier bits rejected"); action.modifiers=1;
    action.label[0]='\n'; CHECK(!validConfig(changed), "nonprintable label rejected"); std::strcpy(action.label,"Copy");
    action.kind=Consumer; CHECK(!validConfig(changed), "keyboard modifier cannot be applied to media"); action.kind=Keyboard;
    CHECK(applyConfig(changed)&&getProfile(ProfileId::Presentation).actions[A].usage==0x06, "runtime profile reads editable config");
    CHECK(getDefaultProfile(ProfileId::Presentation).actions[A].usage==0x3E, "factory preset remains unchanged");
    configLoad(); currentConfig(decoded);
    CHECK(equal(defaults,decoded)&&mock::nvsWrites==0, "blank storage boots defaults without writing mappings");
    CHECK(configSave(changed)&&configRevision()==1&&mock::nvsWrites==1, "save performs one write and updates revision");
    CHECK(configSave(changed)&&configRevision()==1&&mock::nvsWrites==1, "identical save skips flash write");
    applyConfig(defaults); configLoad(); currentConfig(decoded);
    CHECK(equal(changed,decoded), "simulated restart loads saved mappings");
    mock::nvsWriteOk=false;
    CHECK(!configSave(defaults), "storage failure is reported"); currentConfig(decoded);
    CHECK(equal(changed,decoded), "storage failure leaves active mappings unchanged"); mock::nvsWriteOk=true;
    mock::nvs["hh-remote-v3/keys"][30]^=1; configLoad(); currentConfig(decoded);
    CHECK(equal(defaults,decoded), "corrupt stored config safely loads defaults");
    CHECK(std::strstr(configStorageStatus(),"Invalid")!=nullptr, "corrupt-storage fallback is visible");
    mock::nvsOpenOk=false; configLoad();
    CHECK(std::strstr(configStorageStatus(),"unavailable")!=nullptr&&!configSave(changed), "unavailable NVS reported for load and save");
    std::printf("\n%d config/codec/storage checks passed with simulated NVS.\n",checks);
}
