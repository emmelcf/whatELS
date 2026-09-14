#pragma once

/**
 * Shared UI Protocol Definitions
 * 
 * This file acts as the Single Source of Truth for communication between the 
 * Main NanoELS Controller and the LVGL Display Sub-Project.
 * 
 * Using robust semantic strings prevents UI breakage if the UI is rearranged.
 */

// Lathe Telemetry
#define UI_ID_RPM_VAL        "rpm_val"
#define UI_ID_FEED_RATE      "feed_rate"
#define UI_ID_Z_POS          "z_pos"
#define UI_ID_X_POS          "x_pos"
#define UI_ID_SPINDLE_ANGLE  "spindle_ang"

// System Status
#define UI_ID_STATUS_MSG     "status_msg"
#define UI_ID_ERROR_CODE     "error_code"

// Config Parameters
#define UI_ID_PITCH          "pitch"
#define UI_ID_STEPS_PER_REV  "steps_rev"

// Protocol Headers
#define PROTOCOL_HANDSHAKE_PING "PING_LVGL"
#define PROTOCOL_HANDSHAKE_ACK  "ACK_LVGL"
