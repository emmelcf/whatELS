#include "DRO_Scale.h"
#include <driver/pulse_cnt.h>

bool IRAM_ATTR DRO_Scale::pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx) {
    DRO_Scale *scale = static_cast<DRO_Scale*>(user_ctx);
    if (!scale) return false;

    if (edata->watch_point_value >= 32767) {
        scale->overflowCount += 32768;
    } else if (edata->watch_point_value <= -32768) {
        scale->overflowCount -= 32768;
    }
    return false;
}

DRO_Scale::DRO_Scale(gpio_num_t pinA, gpio_num_t pinB, int unit, float scale, int filterValue)
    : aPinNumber(pinA),
      bPinNumber(pinB),
      puType(up),
      filter(filterValue),
      scaleFactor(scale),
      overflowCount(0) {
}

void DRO_Scale::attach() {
    Serial.println("DRO_Scale::attach start");
    Serial.flush();
    pinMode(aPinNumber, puType == up ? INPUT_PULLUP : INPUT);
    pinMode(bPinNumber, puType == up ? INPUT_PULLUP : INPUT);

    pcnt_unit_config_t unitConfig = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    if (pcnt_new_unit(&unitConfig, &pcntUnit) != ESP_OK) {
        return;
    }

    if (filter > 0) {
        pcnt_glitch_filter_config_t filterConfig = {
            .max_glitch_ns = (uint32_t)(filter * 10),
        };
        pcnt_unit_set_glitch_filter(pcntUnit, &filterConfig);
    }

    pcnt_chan_config_t chanConfig = {
        .edge_gpio_num = aPinNumber,
        .level_gpio_num = bPinNumber,
    };
    pcnt_channel_handle_t chan = NULL;
    if (pcnt_new_channel(pcntUnit, &chanConfig, &chan) != ESP_OK) {
        return;
    }

    pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // Watchpoints for 16-bit overflow tracking
    pcnt_unit_add_watch_point(pcntUnit, 32767);
    pcnt_unit_add_watch_point(pcntUnit, -32768);
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_on_reach,
    };
    pcnt_unit_register_event_callbacks(pcntUnit, &cbs, this);

    pcnt_unit_enable(pcntUnit);
    pcnt_unit_clear_count(pcntUnit);
    pcnt_unit_start(pcntUnit);
}

// Returns the raw position from the scale with no offset
float DRO_Scale::getRawPosition() {
    if (!pcntUnit) return 0.0f;
    int currentHardwareCount = 0;
    pcnt_unit_get_count(pcntUnit, &currentHardwareCount);
    int32_t total_count = overflowCount + currentHardwareCount;
    return (float)total_count / scaleFactor;
}

// Returns the final position including the offset
float DRO_Scale::getPosition() {
    return getRawPosition() + positionOffset;
}

// Calculates and stores the new offset
void DRO_Scale::setPosition(float desiredPosition) {
    positionOffset = desiredPosition - getRawPosition();
}

// Zeros the raw count AND the offset
void DRO_Scale::clearCount() {
    overflowCount = 0;
    if (pcntUnit) {
        pcnt_unit_clear_count(pcntUnit);
    }
    positionOffset = 0.0f;
}
