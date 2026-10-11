#pragma once

#include <Arduino.h>

void initUsbKeyboard();
bool usbKeyboardAvailable();
uint16_t usbKeyboardRead();
