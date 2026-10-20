#include "UsbKeyboard.h"
#include "Config.h"

#if ENABLE_USB_KEYBOARD

#include <esp_log.h>
#include <EspUsbHost.h>

static const char* TAG = "USB_KBD";
#include <PS2KeyAdvanced.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define USB_KEY_QUEUE_SIZE 32

static QueueHandle_t usbKeyQueue = NULL;

// Translates standard USB HID Usage Page 0x07 keycodes to equivalent PS2KeyAdvanced scancodes
// so existing whatELS keybindings and numpad logic work seamlessly.
static uint8_t hidToPs2Code(uint8_t hidCode) {
  // Letters A-Z (HID 0x04..0x1D -> PS2_KEY_A..PS2_KEY_Z)
  if (hidCode >= 0x04 && hidCode <= 0x1D) {
    return PS2_KEY_A + (hidCode - 0x04);
  }

  // Digits 1-9 (HID 0x1E..0x26 -> PS2_KEY_1..PS2_KEY_9)
  if (hidCode >= 0x1E && hidCode <= 0x26) {
    return PS2_KEY_1 + (hidCode - 0x1E);
  }

  // Digit 0 (HID 0x27 -> PS2_KEY_0)
  if (hidCode == 0x27) return PS2_KEY_0;

  // Function keys F1-F12 (HID 0x3A..0x45 -> PS2_KEY_F1..PS2_KEY_F12)
  if (hidCode >= 0x3A && hidCode <= 0x45) {
    return PS2_KEY_F1 + (hidCode - 0x3A);
  }

  // Keypad Digits 1-9 (HID 0x59..0x61 -> mapped to PS2_KEY_1..PS2_KEY_9 for numpad entry)
  if (hidCode >= 0x59 && hidCode <= 0x61) {
    return PS2_KEY_1 + (hidCode - 0x59);
  }

  // Keypad Digit 0 (HID 0x62 -> PS2_KEY_0)
  if (hidCode == 0x62) return PS2_KEY_0;

  switch (hidCode) {
    case 0x28: return PS2_KEY_ENTER;     // Enter / Return -> B_ON
    case 0x29: return PS2_KEY_ESC;       // Escape -> B_OFF
    case 0x2A: return PS2_KEY_BS;        // Backspace -> B_BACKSPACE
    case 0x2B: return PS2_KEY_TAB;       // Tab
    case 0x2C: return PS2_KEY_SPACE;     // Spacebar
    case 0x2D: return PS2_KEY_MINUS;     // '-' / '_' -> B_MINUS
    case 0x2E: return PS2_KEY_EQUAL;     // '=' / '+' -> B_PLUS
    case 0x2F: return PS2_KEY_OPEN_SQ;   // '['
    case 0x30: return PS2_KEY_CLOSE_SQ;  // ']'
    case 0x31: return PS2_KEY_BACK;      // '\'
    case 0x33: return PS2_KEY_SEMI;      // ';'
    case 0x34: return PS2_KEY_APOS;      // '\''
    case 0x35: return PS2_KEY_SINGLE;    // '`'
    case 0x36: return PS2_KEY_COMMA;     // ','
    case 0x37: return PS2_KEY_DOT;       // '.'
    case 0x38: return PS2_KEY_DIV;       // '/'

    // Arrow keys
    case 0x4F: return PS2_KEY_R_ARROW;   // Right Arrow -> B_RIGHT
    case 0x50: return PS2_KEY_L_ARROW;   // Left Arrow -> B_LEFT
    case 0x51: return PS2_KEY_DN_ARROW;  // Down Arrow -> B_DOWN
    case 0x52: return PS2_KEY_UP_ARROW;  // Up Arrow -> B_UP

    // Keypad operators
    case 0x54: return PS2_KEY_DIV;       // Keypad '/'
    case 0x55: return PS2_KEY_KP_TIMES;  // Keypad '*'
    case 0x56: return PS2_KEY_MINUS;     // Keypad '-' -> B_MINUS
    case 0x57: return PS2_KEY_EQUAL;     // Keypad '+' -> B_PLUS
    case 0x58: return PS2_KEY_ENTER;     // Keypad Enter -> B_ON
    case 0x63: return PS2_KEY_DOT;       // Keypad '.' -> Dot

    default: return 0;
  }
}

static EspUsbHost usbHost;

void initUsbKeyboard() {
  usbKeyQueue = xQueueCreate(USB_KEY_QUEUE_SIZE, sizeof(uint16_t));
  if (!usbKeyQueue) {
    ESP_LOGE(TAG, "Failed to create usbKeyQueue!");
    return;
  }

  usbHost.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    ESP_LOGD(TAG, "USB Key: %s iface=%u keycode=0x%02X ascii='%c' (0x%02X)",
             event.pressed ? "PRESS" : "RELEASE",
             event.interfaceNumber,
             event.keycode,
             (event.ascii >= 0x20 && event.ascii < 0x7F) ? (char)event.ascii : ' ',
             event.ascii);

    if (!usbKeyQueue) return;
    uint8_t ps2 = hidToPs2Code(event.keycode);
    if (ps2 == 0) return;

    if (event.pressed) {
      uint16_t keyEvent = ps2; // Press event (no PS2_BREAK flag)
      xQueueSend(usbKeyQueue, &keyEvent, 0);
    } else if (event.released) {
      uint16_t keyEvent = ps2 | PS2_BREAK; // Release event
      xQueueSend(usbKeyQueue, &keyEvent, 0);
    }
  });

  usbHost.onConsumerControl([](const EspUsbHostConsumerControlEvent &event) {
    ESP_LOGD(TAG, "USB Consumer: %s usage=0x%04X",
             event.pressed ? "PRESS" : "RELEASE",
             event.usage);
  });

  usbHost.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    ESP_LOGI(TAG, "USB device connected: VID=%04X PID=%04X (%s %s)",
             device.vid, device.pid, device.manufacturer, device.product);
  });

  usbHost.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    ESP_LOGI(TAG, "USB device disconnected: VID=%04X PID=%04X", device.vid, device.pid);
  });

  EspUsbHostConfig config;
  config.taskCore = 0; // Run USB host task on Core 0
  config.taskPriority = 2;

  if (!usbHost.begin(config)) {
    ESP_LOGE(TAG, "usbHost.begin() failed: %s", usbHost.lastErrorName());
  } else {
    ESP_LOGI(TAG, "USB Host initialized");
  }
}

bool usbKeyboardAvailable() {
  if (!usbKeyQueue) return false;
  return uxQueueMessagesWaiting(usbKeyQueue) > 0;
}

uint16_t usbKeyboardRead() {
  uint16_t event = 0;
  if (usbKeyQueue && xQueueReceive(usbKeyQueue, &event, 0) == pdTRUE) {
    return event;
  }
  return 0;
}

#else

void initUsbKeyboard() {}
bool usbKeyboardAvailable() { return false; }
uint16_t usbKeyboardRead() { return 0; }

#endif
