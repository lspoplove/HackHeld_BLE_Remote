#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cxx="${CXX:-c++}"
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -I tests/mocks -I HackHeld_BLE_Remote)
sources=(HackHeld_BLE_Remote/{RemoteCore.cpp,KeyCatalog.cpp,ConfigCodec.cpp,ConfigStore.cpp})
"$cxx" "${flags[@]}" tests/test_config.cpp "${sources[@]}" -o "$work/config"
"$work/config"
"$cxx" "${flags[@]}" tests/test_web.cpp "${sources[@]}" HackHeld_BLE_Remote/WebSetup.cpp -o "$work/web"
# These actual-handler API snapshots are also input to browser rendering tests.
mkdir -p tests/fixtures
"$work/web" tests/fixtures
