#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cxx="${CXX:-c++}"
# First compile the ACTUAL default hardware gate; no GPIO verification is assumed.
"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic -DTEST_UNVERIFIED \
  -I tests/mocks -I HackHeld_BLE_Remote tests/test_app.cpp \
  HackHeld_BLE_Remote/{App.cpp,BleRemote.cpp,RemoteCore.cpp,KeyCatalog.cpp,ConfigCodec.cpp,ConfigStore.cpp,WebSetup.cpp} -o "$work/test_gate"
"$work/test_gate"
# Enable the gate only in a throwaway test copy; shipped BoardConfig stays false.
cp -R HackHeld_BLE_Remote "$work/sketch"
sed -i 's/PINMAP_VERIFIED = false/PINMAP_VERIFIED = true/' "$work/sketch/BoardConfig.h"
"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I tests/mocks -I "$work/sketch" tests/test_app.cpp \
  "$work/sketch/"{App.cpp,BleRemote.cpp,RemoteCore.cpp,KeyCatalog.cpp,ConfigCodec.cpp,ConfigStore.cpp,WebSetup.cpp} -o "$work/test_app"
"$work/test_app"
