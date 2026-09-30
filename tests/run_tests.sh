#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -I HackHeld_BLE_Remote \
  tests/test_core.cpp HackHeld_BLE_Remote/{RemoteCore.cpp,KeyCatalog.cpp} -o "$work/test_core"
"$work/test_core"
python3 tests/check_hid_descriptor.py
