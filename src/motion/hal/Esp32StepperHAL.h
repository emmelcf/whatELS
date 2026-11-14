#pragma once

/**
 * @file Esp32StepperHAL.h
 * @brief Concrete ESP32-S3 GPTimer/RMT stepper pulse generation HAL.
 * @see docs/motion/HAL_SPECIFICATION.md
 * @see docs/motion/CONCURRENCY_AND_TIMING.md
 */

#include "IStepperHAL.h"
#include <cstdint>
#include <cstddef>

#if defined(ESP32) || defined(ARDUINO)
#include <esp_attr.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
typedef void* gptimer_handle_t;
struct gptimer_alarm_event_data_t;
#endif

/**
 * @brief Internal hardware descriptor for a single stepper axis.
 */
struct AxisHardwareState {
  int stepPin = -1;
  int dirPin = -1;
  int enablePin = -1;
  int faultPin = -1;
  StepTimingConfig timing;
  bool configured = false;
  bool enabled = false;
  int8_t currentDir = 1;
  int64_t enableTimestampUs = 0;
  int64_t lastDirChangeTimestampUs = 0;
  int64_t lastStepTimestampUs = 0;
  bool pulseTrainActive = false;
  uint32_t pulseFrequencyHz = 0;
  gptimer_handle_t timerHandle = nullptr;
};

class Esp32StepperHAL;

/**
 * @brief Context passed to timer alarm ISR callback.
 */
struct StepperTimerContext {
  Esp32StepperHAL* hal = nullptr;
  AxisId axis = AXIS_Z;
};

/**
 * @brief Concrete ESP32-S3 Hardware Stepper Driver.
 * @details Conforms to HAL_SPECIFICATION.md Section 2 and StepTimingConfig.
 *          Implements nanosecond timing guard adherence, non-blocking enable settling,
 *          and dedicated hardware GPTimer pulse trains.
 */
class Esp32StepperHAL : public IStepperHAL {
public:
  static constexpr size_t MAX_AXES = 2;

  Esp32StepperHAL();
  virtual ~Esp32StepperHAL();

  /**
   * @brief Configures physical GPIO pins and timing constraints for an axis.
   */
  bool configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) override;

  /**
   * @brief Configures an optional hardware driver fault input pin.
   */
  bool configureFaultPin(AxisId axis, int faultPin);

  /**
   * @brief Sets physical motion direction with hold-time guard enforcement.
   */
  void setDirection(AxisId axis, int8_t dir) override;

  /**
   * @brief Returns current latched direction polarity.
   */
  int8_t getDirection(AxisId axis) const override;

  /**
   * @brief Enables/disables axis with non-blocking settling timestamp latch.
   */
  void setEnabled(AxisId axis, bool enable) override;

  /**
   * @brief Returns whether axis stepper is energized.
   */
  bool isEnabled(AxisId axis) const override;

  /**
   * @brief Fires a single step pulse strictly enforcing setup and width timing.
   */
  void pulseStep(AxisId axis) override;

  /**
   * @brief Starts an asynchronous GPTimer-driven pulse train.
   */
  void startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) override;

  /**
   * @brief Dynamically updates pulse rate without interrupting motion.
   */
  void updatePulseFrequency(AxisId axis, uint32_t frequencyHz) override;

  /**
   * @brief Halts the asynchronous pulse train immediately.
   */
  void stopPulseTrain(AxisId axis) override;

  /**
   * @brief Returns true if an asynchronous pulse train is active.
   */
  bool isPulseTrainActive(AxisId axis) const override;

  /**
   * @brief Returns true if hardware driver fault pin is asserted.
   */
  bool hasDriverFault(AxisId axis) const override;

  /**
   * @brief Inspects axis hardware state (diagnostics / telemetry).
   */
  const AxisHardwareState& getAxisState(AxisId axis) const;

#if defined(ESP32) || defined(ARDUINO)
  /**
   * @brief Static GPTimer alarm callback.
   */
  static bool IRAM_ATTR onTimerAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx);
#endif

private:
  AxisHardwareState   m_axes[MAX_AXES];
  StepperTimerContext m_timerContexts[MAX_AXES];

  bool initTimer(size_t axisIdx);
  void cleanupTimer(size_t axisIdx);
};
