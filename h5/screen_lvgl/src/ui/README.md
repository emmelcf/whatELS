# UI Export Directory

This directory (`h5/screen_lvgl/src/ui/`) is the designated export path for the LVGL Editor.

When you configure the LVGL Editor to generate C/C++ code, set this folder as your export destination. It will automatically generate files such as:
* `ui.h`
* `ui.c`
* `ui_events.c`
* and associated font/image assets.

**Remember:** If you create Custom Events (Call Functions) in the editor, you will need to open the generated `ui_events.c` file and include `"../shared_protocol/ui_telemetry.h"` to send semantic IDs over UART/WebSockets!
