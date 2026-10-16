#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <esp_attr.h>

/**
 * @brief Snapshot of motion controller state passed from Core 1 to Core 0.
 * @details Conforms to CONCURRENCY_AND_TIMING.md Section 3.1.
 */
struct MotionTelemetrySnapshot {
  int32_t  zPosSteps;        // Current leadscrew step position
  int32_t  zCoordDu;         // Workpiece coordinate in deci-microns
  int32_t  xPosSteps;        // Current cross-slide step position
  int32_t  xCoordDu;         // Workpiece coordinate in deci-microns
  uint32_t spindleRpm;       // Filtered RPM
  int64_t  spindlePulses;    // Raw continuous spindle encoder pulses
  uint16_t motionMode;       // Active MotionMode enum
  bool     isMoving;         // Active stepping flag
  bool     isFeedHold;       // Pause flag
  uint8_t  activeLimitSlot;  // Limit currently being approached or rested on (0 if none)
  int32_t  distToLimitDu;    // Remaining distance to stop
  uint32_t sequenceNumber;   // Monotonic sequence tag for atomic consistency
};

/**
 * @brief Lock-free seqlock mailbox for streaming Core 1 telemetry to Core 0 supervisory tasks.
 * @details Conforms to [CONC-01] and CONCURRENCY_AND_TIMING.md Section 3.1.
 *          Uses explicit memory fences to guarantee tear-free reads across dual cores.
 */
class TelemetryMailbox {
public:
  void IRAM_ATTR publish(const MotionTelemetrySnapshot& snapshot) {
    uint32_t seq = m_seq.load(std::memory_order_relaxed);
    m_seq.store(seq + 1, std::memory_order_relaxed); // Mark odd (write in progress)

    // Prevent compiler or CPU from reordering payload writes before (seq + 1) store
    std::atomic_thread_fence(std::memory_order_release);

    std::memcpy(const_cast<MotionTelemetrySnapshot*>(&m_data), &snapshot, sizeof(MotionTelemetrySnapshot));

    // Ensure all payload bytes are globally visible before sequence counter is marked even
    m_seq.store(seq + 2, std::memory_order_release);
  }

  bool read(MotionTelemetrySnapshot& outSnapshot) const {
    for (int retries = 0; retries < 4; ++retries) {
      uint32_t seq1 = m_seq.load(std::memory_order_acquire);
      if (seq1 & 1U) {
        continue; // Writer active; retry
      }

      std::memcpy(&outSnapshot, const_cast<const MotionTelemetrySnapshot*>(&m_data), sizeof(MotionTelemetrySnapshot));

      // Prevent payload reads from sinking below seq2 load
      std::atomic_thread_fence(std::memory_order_acquire);

      uint32_t seq2 = m_seq.load(std::memory_order_relaxed);
      if (seq1 == seq2) {
        return true; // Guaranteed tear-free snapshot
      }
    }
    return false;
  }

private:
  std::atomic<uint32_t>            m_seq{0};
  volatile MotionTelemetrySnapshot m_data{};
};
