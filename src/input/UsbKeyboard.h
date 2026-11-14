#pragma once

/**
 * @file UsbKeyboard.h
 * @brief USB HID keyboard input adapter interface.
 * @see docs/WHATELS_MASTER_ARCHITECTURE.md
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 */

#include <Arduino.h>

void initUsbKeyboard();
bool usbKeyboardAvailable();
uint16_t usbKeyboardRead();
