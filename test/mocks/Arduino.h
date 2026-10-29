#pragma once
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>

using String = std::string;

#ifndef PROGMEM
#define PROGMEM
#endif

inline unsigned long millis() { return 0; }
inline uint64_t esp_timer_get_time() { return 0; }
inline void delay(unsigned long) {}
inline void yield() {}
