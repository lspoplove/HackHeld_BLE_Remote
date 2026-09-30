#pragma once
#include <stdint.h>

namespace board {
// Reference: DSTIKE's OLED / six-button HackHeld32S3 page and Snake Game.ino.
// NOT the ST7789 / eight-button model and NOT HackHeld32C5.
constexpr int OLED_SDA = 41;
constexpr int OLED_SCL = 42;
constexpr uint8_t OLED_ADDRESS = 0x3C; // 7-bit address; 0x3D is also probed.
constexpr int BUTTON_PINS[6] = {38, 35, 36, 37, 19, 20}; // Up, Down, Left, Right, A, B
constexpr bool BUTTON_ACTIVE_LOW = true;
constexpr int RGB_PIN = 1;
constexpr int BUZZER_PIN = 5;
constexpr int SECOND_BUZZER_PIN = 40;

// IMPORTANT HARDWARE GATE:
// GPIO35-37 are reserved on common ESP32-S3 N16R8 / octal-PSRAM modules.
// GPIO19/20 are also the native USB data pins. The published pin map and
// published memory configuration are not sufficient to establish safe wiring.
// Check the actual schematic/module and correct BUTTON_PINS FIRST.
// Set true ONLY after verifying every pin is usable on this hardware revision.
// Disabling PSRAM does NOT prove that reserved module pads become usable.
constexpr bool PINMAP_VERIFIED = true;

// Enable diagnostic UART logging only after verifying the programming path.
// No Serial.begin() and no native USB runtime are needed for BLE operation.
constexpr bool UART_LOG = false;
constexpr uint32_t I2C_HZ = 400000;
constexpr uint32_t DEBOUNCE_MS = 18;
constexpr uint32_t OLED_DIM_AFTER_MS = 60000;
constexpr uint8_t RGB_LEVEL = 10; // intentionally low brightness
constexpr char DEVICE_PREFIX[] = "DSTIKE Remote";
}
