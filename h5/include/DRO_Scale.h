#ifndef _DRO_SCALE_h
#define _DRO_SCALE_h

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pcnt.h>

#define MAX_ENCODERS 8

enum encType { full };
enum pullupType { up };

class DRO_Scale {
private:
    gpio_num_t aPinNumber;
    gpio_num_t bPinNumber;
    pullupType puType;
    pcnt_unit_t pcntUnit;
    int filter;
    float scaleFactor;
    float positionOffset = 0.0f; // Stores the offset for Known Diameter

    static volatile int32_t overflowCount[MAX_ENCODERS];
    static void IRAM_ATTR pcnt_intr_handler(void* arg);

public:
    DRO_Scale(gpio_num_t pinA, gpio_num_t pinB, pcnt_unit_t unit, float scale, int filterValue = 100);

    void attach();
    float getPosition();     // Gets the final position (Raw + Offset)
    void clearCount();       // Zeros both the raw count and the offset
    
    // --- New Functions for Known Diameter ---
    float getRawPosition();  // Gets the raw, un-offseted position
    void setPosition(float desiredPosition); // Calculates and sets the new offset
};

#endif
