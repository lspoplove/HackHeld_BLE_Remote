#!/usr/bin/env python3
"""Validate sizes in the actual HID report descriptor, without a BLE adapter."""
import re
from pathlib import Path
source = (Path(__file__).parent.parent / 'HackHeld_BLE_Remote' / 'BleRemote.cpp').read_text()
block = re.search(r'uint8_t reportMap\[\] = \{(.*?)\};', source, re.S).group(1)
data = bytes(int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', block))
report_id = size = count = 0
bits = {}
i = 0
while i < len(data):
    prefix = data[i]
    i += 1
    if prefix == 0xFE:
        raise AssertionError('Unexpected long item')
    length = (0, 1, 2, 4)[prefix & 3]
    value = int.from_bytes(data[i:i+length], 'little')
    assert i + length <= len(data)
    i += length
    kind = (prefix >> 2) & 3
    tag = prefix >> 4
    if kind == 1:
        if tag == 7: size = value
        elif tag == 8: report_id = value
        elif tag == 9: count = value
    elif kind == 0 and tag in (8, 9, 11):
        key = (report_id, {8: 'input', 9: 'output', 11: 'feature'}[tag])
        bits[key] = bits.get(key, 0) + size * count
assert bits == {(1, 'input'): 64, (1, 'output'): 8, (2, 'input'): 16}, bits
print('PASS HID report 1 input: 8 bytes; output: 1 byte; report 2 input: 2 bytes')
print('This validates descriptor lengths only, not host BLE/HID compatibility.')
