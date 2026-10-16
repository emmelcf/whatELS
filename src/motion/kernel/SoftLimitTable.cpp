#include "SoftLimitTable.h"
#include <cstring>

const SoftLimit SoftLimitTable::s_emptyLimit{};

SoftLimitTable::SoftLimitTable() {
  clearAll();
}

bool SoftLimitTable::setLimit(size_t slot, int32_t coordDu, int32_t stepPos, const char* label, bool enable) {
  if (slot >= MAX_LIMITS) {
    return false;
  }

  m_limits[slot].id      = static_cast<uint8_t>(slot + 1);
  m_limits[slot].defined = true;
  m_limits[slot].enabled = enable;
  m_limits[slot].coordDu = coordDu;
  m_limits[slot].stepPos = stepPos;

  if (label != nullptr) {
    std::strncpy(m_limits[slot].label, label, sizeof(m_limits[slot].label) - 1);
    m_limits[slot].label[sizeof(m_limits[slot].label) - 1] = '\0';
  } else {
    m_limits[slot].label[0] = '\0';
  }

  return true;
}

bool SoftLimitTable::setEnabled(size_t slot, bool enable) {
  if (slot >= MAX_LIMITS || !m_limits[slot].defined) {
    return false;
  }
  m_limits[slot].enabled = enable;
  return true;
}

bool SoftLimitTable::clearLimit(size_t slot) {
  if (slot >= MAX_LIMITS) {
    return false;
  }
  m_limits[slot] = SoftLimit{};
  m_limits[slot].id = static_cast<uint8_t>(slot + 1);
  return true;
}

void SoftLimitTable::clearAll() {
  for (size_t i = 0; i < MAX_LIMITS; ++i) {
    m_limits[i] = SoftLimit{};
    m_limits[i].id = static_cast<uint8_t>(i + 1);
  }
}

void SoftLimitTable::updateOrigin(const AxisState& axis) {
  for (size_t i = 0; i < MAX_LIMITS; ++i) {
    if (m_limits[i].defined) {
      m_limits[i].coordDu = stepsToDu(axis.geometry, m_limits[i].stepPos + axis.originSteps);
    }
  }
}

const SoftLimit& SoftLimitTable::getSlot(size_t slot) const {
  if (slot >= MAX_LIMITS) {
    return s_emptyLimit;
  }
  return m_limits[slot];
}
