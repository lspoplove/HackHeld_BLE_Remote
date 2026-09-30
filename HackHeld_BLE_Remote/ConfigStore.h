#pragma once
#include "RemoteCore.h"
namespace remote {
void configLoad();
bool configSave(const RemoteConfig& config);
uint32_t configRevision();
const char* configStorageStatus();
}
