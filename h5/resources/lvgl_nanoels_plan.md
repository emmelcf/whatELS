# Comprehensive Implementation Plan: Responsive Dual-MCU LVGL Display for NanoELS

This plan outlines the architecture for a low-maintenance, multi-resolution LVGL HMI display powered by an integrated ESP32 variant (S3 or P4). It runs completely decoupled from the main NanoELS lathe controller via a high-speed, low-latency, shielded hardware UART interface utilizing Nextion-compatible text streams.

---

## Phase 1: Image Asset Pipeline Configuration (Hybrid Build/Runtime)

To minimize firmware bloat while fully automating cross-resolution image rendering across screens ranging from 5" to 10.1", a two-tier hybrid scaling architecture will be established.

### Step 1.1: Build-Time Maximum Downsampling (PC Pipeline)
Instead of embedding raw, high-resolution desktop images, a pre-build script optimizes and bounds all visual assets to a hard ceiling matching the largest supported display option (e.g., 1024x600 for a 10.1" panel).

```python
# scripts/pre_scale_assets.py
import os
from PIL import Image

SOURCE_DIR = "./assets/raw_desktop_images"
OUTPUT_DIR = "./main/generated_assets"
MAX_SUPPORTED_RES = (1024, 600)  # Ceiling limit for maximum panel target

def pre_scale_to_max():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    for filename in os.listdir(SOURCE_DIR):
        if not filename.lower().endswith(('.png', '.jpg', '.jpeg')):
            continue
        img_path = os.path.join(SOURCE_DIR, filename)
        base_name = os.path.splitext(filename)[0]
        
        with Image.open(img_path) as img:
            img.thumbnail(MAX_SUPPORTED_RES, Image.Resampling.LANCZOS)
            output_path = os.path.join(OUTPUT_DIR, f"{base_name}_max.png")
            img.save(output_path, "PNG")
            print(f"[Build Script] Pre-scaled asset: {output_path} ({img.width}x{img.height})")

if __name__ == "__main__":
    pre_scale_to_max()
```

### Step 1.2: First-Boot Automated Flash Caching (On-Device Local Scale)
When the display powers up, it programmatically checks if its connected physical resolution matches the compiled master assets. If it is smaller (e.g., a 5" or 7" 800x480 screen), it drops into a one-time scaling routine, draws the optimized down-sampled grid inside an offscreen scratchpad canvas, and streams raw uncompressed `.bin` structures permanently onto its onboard LittleFS file system partition.

```c
// main/src/asset_manager.c
#include "lvgl.h"
#include <stdio.h>
#include "esp_littlefs.h"

LV_IMAGE_DECLARE(img_logo_max); // 1024x600 ceiling asset compiled in flash

void check_and_scale_on_boot(void) {
    int32_t physical_w = lv_display_get_horizontal_resolution(NULL);
    // Use a version identifier in the filename so future OTA firmware updates
    // properly trigger a cache invalidation and rebuild of the scaled asset.
    const char* target_file_path = "/littlefs/ui_logo_fitted_v2.bin";

    // 1. Skip if screen size naturally matches the maximum compiled ceiling resolution
    if (physical_w >= img_logo_max.header.w) return; 

    // 2. Short-circuit if this custom-scaled binary asset was already cached on a past boot
    FILE *f = fopen(target_file_path, "r");
    if (f != NULL) {
        fclose(f);
        return; 
    }

    // 3. Render a clean UI initialization message to signal the one-time operation
    lv_obj_t * label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Initializing custom display assets...
Please wait.");
    lv_obj_center(label);
    lv_timer_handler(); 

    // 4. Compute optimal smaller proportions relative to physical constraints
    int32_t target_w = physical_w / 2; // Always span exactly half the hardware viewport width
    int32_t target_h = (img_logo_max.header.h * target_w) / img_logo_max.header.w;
    uint32_t buffer_size = target_w * target_h * 2; // RGB565 = 2 Bytes Per Pixel

    // 5. Allocate heap bounds explicitly into PSRAM to avoid exhausting internal SRAM
    // For ESP-IDF, include <esp_heap_caps.h>
    uint8_t *scratchpad = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    if (!scratchpad) return;

    lv_obj_t *canvas = lv_canvas_create(NULL);
    lv_canvas_set_buffer(canvas, scratchpad, target_w, target_h, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_image_dsc_t draw_dsc;
    lv_draw_image_dsc_init(&draw_dsc);
    draw_dsc.src = &img_logo_max;
    draw_dsc.scale_x = (target_w * 256) / img_logo_max.header.w; // 256 matrix scale
    draw_dsc.scale_y = draw_dsc.scale_x;
    draw_dsc.antialias = 1;

    lv_area_t coords = {0, 0, target_w - 1, target_h - 1};
    lv_draw_image(&layer, &draw_dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);

    // 6. Persist down-scaled matrix directly to internal flash blocks
    f = fopen(target_file_path, "w");
    if (f != NULL) {
        lv_image_header_t header = {
            .cf = LV_COLOR_FORMAT_RGB565,
            .w = target_w,
            .h = target_h,
            .stride = target_w * 2,
            .magic = 0x14 // Official LVGL native binary signature
        };
        fwrite(&header, sizeof(lv_image_header_t), 1, f);
        fwrite(scratchpad, 1, buffer_size, f);
        fclose(f);
    }

    // 7. Flush temporary scaling objects completely from RAM
    lv_obj_delete(canvas);
    free(scratchpad);
    lv_obj_clean(lv_screen_active());
}
```

---

## Phase 2: Core Firmware & Unified Cross-Platform Project Configuration

To ensure one single codebase compiles effortlessly across various brands (Sunton, Waveshare, Elecrow) and processor families (ESP32-S3 vs. ESP32-P4), the hardware variables are modularized inside environment definitions.

### Step 2.1: Unified PlatformIO Multi-Target Mapping (`platformio.ini`)
```ini
[platformio]
default_envs = sunton_s3_7inch

[env]
platform = espressif32
framework = arduino
lib_deps = lvgl/lvgl@^9.1.0
build_src_filter = +<*> +<shared_ui/>

[env:sunton_s3_7inch]
board = esp32-s3-devkitc-1
build_flags = 
    -D BOARD_SUNTON_S3
    -D SCREEN_WIDTH=800
    -D SCREEN_HEIGHT=480
    -D BOARD_HAS_PSRAM
    -D RX_PIN=16
    -D TX_PIN=17

[env:waveshare_p4_10inch]
board = esp32-p4-devkitc
build_flags = 
    -D BOARD_WAVESHARE_P4
    -D SCREEN_WIDTH=1024
    -D SCREEN_HEIGHT=600
    -D BOARD_HAS_32MB_PSRAM
    -D RX_PIN=4
    -D TX_PIN=5
```

### Step 2.2: Universal Layout Design (Percentage & Auto-Grid Scaling)
To prevent elements from bunching up when swapping display panels, all positions, fonts, and bounding grids must use relational rules.

```c
// shared_ui/ui_dashboard.c
#include "lvgl.h"

lv_obj_t *lbl_rpm;
lv_obj_t *lbl_feed_rate;

void create_lathe_dashboard_ui(void) {
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    
    // Create an adaptive Flexbox parent grid across the window
    lv_obj_t * grid = lv_obj_create(lv_screen_active());
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Scaled Dynamic Asset Widget Selection
    lv_obj_t * img = lv_image_create(grid);
    if (screen_w >= 1024) {
        lv_image_set_src(img, &img_logo_max); // Native compiled flash map
    } else {
        lv_image_set_src(img, "F:/ui_logo_fitted_v2.bin"); // Dynamic Flash Local Storage file
    }

    // RPM Telemetry Box (Proportional 40% Width layout block)
    lv_obj_t * rpm_box = lv_obj_create(grid);
    lv_obj_set_size(rpm_box, lv_pct(40), lv_pct(80));
    
    lbl_rpm = lv_label_create(rpm_box);
    lv_label_set_text(lbl_rpm, "0");
    // Dynamically adjust font selection metrics per display scale class
    if (screen_w >= 1024) {
        lv_obj_set_style_text_font(lbl_rpm, &lv_font_montserrat_48, 0);
    } else {
        lv_obj_set_style_text_font(lbl_rpm, &lv_font_montserrat_28, 0);
    }
    lv_obj_center(lbl_rpm);
}
```

---

## Phase 3: Hardware Interface & Shielded Serial UART Protocol Line

Since the Main NanoELS controller's internal radio stack is reserved for handling external Wi-Fi diagnostic connections, all local display syncing is executed via hardware pins using a zero-overhead Nextion packet emulator.

### Step 3.1: Ultra-Low Latency Nextion Emulator Loop
This block parses continuous incoming asynchronous telemetry records instantly, processing numerical strings without stalling active user interaction.

```c
// main/src/serial_parser.c
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <HardwareSerial.h>

extern lv_obj_t *lbl_rpm;
#define RX_BUF_SIZE 128
char rx_buffer[RX_BUF_SIZE];
int rx_index = 0;

void parse_nextion_command(const char* cmd) {
    // Process Lathe RPM updates (Pattern: txt_rpm.txt="1250")
    if (strncmp(cmd, "txt_rpm.txt=", 12) == 0) {
        const char* start = strchr(cmd, '"');
        if (start) {
            start++; 
            const char* end = strchr(start, '"');
            if (end) {
                char value[32] = {0};
                // Safely bound string copy to prevent buffer overflow
                size_t len = end - start;
                if (len >= sizeof(value)) len = sizeof(value) - 1;
                strncpy(value, start, len);
                
                // IMPORTANT: LVGL is not thread-safe! 
                // Ensure check_serial_input() is called from the same loop() as lv_timer_handler()
                // OR wrap this with lv_lock() and lv_unlock() if running in a FreeRTOS task.
                lv_label_set_text(lbl_rpm, value); // Direct UI frame manipulation
            }
        }
    }
}

uint8_t ff_count = 0;

void check_serial_input(void) {
    while (Serial2.available() > 0) {
        char c = Serial2.read();
        
        // Nextion protocol utilizes three consecutive 0xFF bytes as End Of Message
        if ((uint8_t)c == 0xFF) {
            ff_count++;
            if (ff_count == 3) {
                if (rx_index > 0) {
                    rx_buffer[rx_index] = '\0';
                    parse_nextion_command(rx_buffer);
                    rx_index = 0; 
                }
                ff_count = 0; // Reset for next message
                break;
            }
        } else {
            ff_count = 0; // Reset if interrupted by a non-0xFF byte
            if (rx_index < RX_BUF_SIZE - 1) {
                rx_buffer[rx_index++] = c;
            } else {
                rx_index = 0; // Clear bounds overflow to avoid memory corruption
            }
        }
    }
}
```

### Step 3.2: Critical Noise Abatement & Shielding Wiring Guidelines
To completely counter severe high-frequency electromagnetic interference generated by Lathe Spindle VFD lines and switching driver loads, strictly adhere to these wiring laws:

```
        MAIN NANOELS CONTROLLER                INTEGRATED ESP32 DISPLAY
     +---------------------------+          +---------------------------+
     |                           |          |                           |
     |         TX (GPIO) --------|--------->| RX (RX_PIN)               |
     |         RX (GPIO) <-------|----------| TX (TX_PIN)               |
     |         GND       --------|--------->| GND                       |
     |                           |          |                           |
     +---------------------------+          +---------------------------+
                   |                                      |
                   v                                      v
       ========================================================
      [    INNER TWISTED DATA CORE (Shielding Braid Outer)     ]
       ========================================================
                   |
                   +--- SHIELD DRAIN WIRE CONNECTED TO GROUND 
                        ON THE MAIN CONTROLLER SIDE ONLY!
                        (Leave display side disconnected to avoid ground loops)
```

1. **Keep Physical Lines Minimal:** Keep the unshielded portion of the data wire lengths under **4 to 6 inches**.
2. **Implement Twisted Shielded Infrastructure:** Use shielded twisted-pair cables (such as a scrap section of a shielded Cat6 STP patch cable or high-fidelity microphone signal wiring).
3. **Single-Ended Ground Termination:** Connect the metallic shielding braid drain line **strictly to a GND terminal on the Main NanoELS controller motherboard interface side only**. Leave the shield completely detached/floating on the display unit frame box enclosure. Breaking this continuity prevents dangerous stray high-current ground loops from running along your communication link.
4. **Configure Elevated Baud Rates:** Initialize hardware ports on both nodes to **115200** or **230400 bps** (`SERIAL_8N1`). This minimizes serialization flight delays and drops execution tracking intervals to clear **sub-millisecond latencies**.

---

## Phase 4: Critical Implementation Considerations

While the architecture outlines the overall structure, these system-level constraints must be strictly adhered to during development:

### 4.1 LVGL Thread Safety
LVGL APIs (like `lv_label_set_text`) are **not thread-safe**. If `check_serial_input()` processes data inside a dedicated FreeRTOS UART task or `serialEvent()` interrupt, concurrently interacting with LVGL objects will eventually lead to memory corruption or crashes. 
* **Solution**: Ensure serial polling runs on the exact same thread/context as `lv_timer_handler()`. If a separate RTOS task is mandatory for high-performance serial buffering, you must wrap all LVGL function calls in mutexes (`lv_lock()` / `lv_unlock()`).

### 4.2 PSRAM Forced Allocation
The ESP32 platform defaults standard `malloc()` requests to internal SRAM, which typically only has about 320-512KB available. A 512x300 RGB565 canvas array requires ~307KB contiguous RAM, which will likely fail or heavily fragment the internal memory.
* **Solution**: As implemented in the revised code, using `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` guarantees that large, transient graphics buffers allocate strictly onto the external 2MB-32MB SPI RAM chip.

### 4.3 Deterministic Cache Invalidation
The scaled `.bin` cache checking skips rendering if a file already exists on LittleFS. This poses an issue for OTA firmware updates where the base `img_logo_max` asset is changed, but the display still relies on the outdated cache file.
* **Solution**: Appending a version number directly to the cached filename (e.g., `ui_logo_fitted_v2.bin`) ensures that rolling out an update securely orphans the old file and forces a one-time clean regeneration on the next boot.

### 4.4 Defensive Serial Protocols
Because UART lacks strict synchronization, assuming a single `0xFF` byte denotes the end of a frame or using `Serial2.peek()` introduces race conditions. The Nextion protocol correctly sends three sequential `0xFF`s. The implemented state machine guarantees the packet is complete before passing bounded length payloads into string processing utilities (preventing buffer overflow stack attacks).

---

## Phase 5: Display Auto-Detection & UI Naming Abstraction (Main Controller)

Currently, the main NanoELS firmware updates Nextion displays using auto-assigned integer IDs, which are brittle and frequently break if the Nextion UI is rearranged in the editor. To support the new LVGL display without breaking legacy Nextion compatibility, a hardware abstraction layer (HAL) should be introduced on the **Main NanoELS Controller**.

### 5.1 Component Naming Strategy
The main firmware should transition its core telemetry logic to use robust, semantic internal string names for all UI elements (e.g., `rpm_val`, `feed_rate`, `status_msg`).
* For **LVGL Displays**, the main controller will transmit updates using these robust semantic keys directly.
* For **Legacy Nextion Displays**, a translation mapping table will be maintained to convert the semantic key back to the fragile integer ID (or specific Nextion object name) required by the old screen sets.

### 5.2 Display Auto-Detection Handshake
To dynamically route telemetry through the correct protocol without requiring the user to change configuration settings, the main controller should attempt a handshake sequence at boot.

```cpp
// Example Pseudocode for Main Controller Boot
enum DisplayType { DISPLAY_NONE, DISPLAY_NEXTION, DISPLAY_LVGL };
DisplayType current_display = DISPLAY_NONE;

void detect_display_hardware() {
    // 1. Attempt to Ping the LVGL Display using a custom handshake command
    Serial2.print("PING_LVGL\n");
    delay(100);
    if (Serial2.available()) {
        String response = Serial2.readStringUntil('\n');
        if (response.indexOf("ACK_LVGL") >= 0) {
            current_display = DISPLAY_LVGL;
            return;
        }
    }

    // 2. Fallback to standard Nextion connection protocol
    Serial2.print("\xFF\xFF\xFF"); // Clear buffers
    Serial2.print("connect\xFF\xFF\xFF");
    // Parse response. If valid Nextion connect payload -> DISPLAY_NEXTION
}
```

### 5.3 Abstraction/Translation Layer
When the core lathe logic needs to update the UI, it calls a unified abstraction function, which then handles the routing:

```cpp
// Main Controller Abstraction Layer
void update_ui_element(const char* semantic_name, const char* value) {
    if (current_display == DISPLAY_LVGL) {
        // Send robust semantic packet to LVGL display (e.g. JSON or Key-Value)
        // Example: rpm_val="1250"
        Serial2.printf("%s=\"%s\"\n", semantic_name, value);
    } 
    else if (current_display == DISPLAY_NEXTION) {
        // Look up the legacy Nextion ID based on the robust semantic name
        int legacy_id = lookup_nextion_id(semantic_name);
        if (legacy_id >= 0) {
            // Send standard Nextion formatted update (e.g., t0.txt="1250")
            Serial2.printf("t%d.txt=\"%s\"\xFF\xFF\xFF", legacy_id, value);
        }
    }
}
```

By isolating this inside a display HAL, the core CNC state machine remains clean and completely agnostic to the physical display hardware connected to it.
