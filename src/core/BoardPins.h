#pragma once

#include <driver/gpio.h>

/**
 * =============================================================================
 * whatELS - Hardware Pinout & Board Definitions (whatELS Controller PCB)
 * =============================================================================
 *
 * Physical PCB trace mappings for the ESP32-S3 whatELS controller board.
 * WARNING: These pin assignments map to physical copper traces on the hardware PCB.
 *          Do not reassign without hardware PCB revision and confirmation.
 * =============================================================================
 */

// Spindle Optical Rotary Encoder
#define ENC_A 14
#define ENC_B 13

// Main Lead Screw (Z) Stepper / Servo Pins
#define Z_ENA 41
#define Z_DIR 42
#define Z_STEP 35
#define Z_PULSE_A 18
#define Z_PULSE_B 8

// Cross-Slide Lead Screw (X) Stepper / Servo Pins
#define X_ENA 16
#define X_DIR 15
#define X_STEP 7
#define X_PULSE_A 47
#define X_PULSE_B 21

// whatELS Controller PCB Silkscreen Hardware Pin Headers (Reserved / Unpopulated)
#define Y_ENA 1
#define Y_DIR 2
#define Y_STEP 17
#define Y_PULSE_A 45
#define Y_PULSE_B 48

// DRO Linear Scales Pinouts (Quadrature Encoder interface)
#define Z_SCALE_PIN_A (gpio_num_t)9
#define Z_SCALE_PIN_B (gpio_num_t)12
#define Z_SCALE_PCNT_UNIT 1
#define Z_SCALE_PULSES_PER_MM 1250.67f

#define X_SCALE_PIN_A (gpio_num_t)40
#define X_SCALE_PIN_B (gpio_num_t)39
#define X_SCALE_PCNT_UNIT 2
#define X_SCALE_PULSES_PER_MM 501.2f

// Debug UART via MISC Header (for Rainbowlink v2 / bench debugging)
#define DEBUG_UART_TX_PIN 10
#define DEBUG_UART_RX_PIN 11
#define DEBUG_UART_BAUD 115200
