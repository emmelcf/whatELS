#ifndef _DRO_SCALE_h
#define _DRO_SCALE_h

/**
 * @file DRO_Scale.h
 * @brief Hardware PCNT quadrature linear glass scale (DRO) driver.
 * @see docs/motion/HAL_SPECIFICATION.md
 * @see docs/machine/LATHE_MACHINE_PROFILE.md
 */

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>

#define MAX_ENCODERS 8

enum encType { full };
enum pullupType { up };

class DRO_Scale {
private:
    gpio_num_t aPinNumber;
    gpio_num_t bPinNumber;
    pullupType puType;
    pcnt_unit_handle_t pcntUnit = NULL;
    int filter;
    float scaleFactor;
    float positionOffset = 0.0f; // Stores the offset for Known Diameter
    volatile int32_t overflowCount = 0;

    static bool IRAM_ATTR pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx);

public:
    DRO_Scale(gpio_num_t pinA, gpio_num_t pinB, int unit, float scale, int filterValue = 100);

    void attach();
    float getPosition();     // Gets the final position (Raw + Offset)
    void clearCount();       // Zeros both the raw count and the offset
    
    // --- New Functions for Known Diameter ---
    float getRawPosition();  // Gets the raw, un-offseted position
    void setPosition(float desiredPosition); // Calculates and sets the new offset
    void setScaleFactor(float factor);       // Updates scale factor (pulses/mm)
    float getScaleFactor() const;            // Gets current scale factor
};

extern DRO_Scale xScale;
extern DRO_Scale zScale;

#endif
