# ManPro boot splash for CrowPanel ESP32S3 (LVGL v8)

The HTML file was for previewing the look. It can't run on the device itself —
ESP32S3/ESP32-WROOM boards don't have a browser. This is the same animation
rebuilt natively in LVGL, which is what CrowPanel's Arduino examples already
use for the UI.

## Files

- `manpro_logo.c` — the logo as an LVGL image asset (480×192px, RGB565, ~180KB).
  It's pre-flattened onto the splash background color, so no alpha channel is
  needed (keeps it small and fast to draw).
- `manpro_logo.h` — header declaring that asset.
- `manpro_splash.cpp` / `manpro_splash.h` — builds the LVGL screen and runs the
  same beats as the HTML version: wipe-in, scale settle, underline sweep,
  "INITIALIZING SYSTEM" + pulsing dots, then tears itself down after 3s.

## 1. Check your `lv_conf.h`

These must be set (CrowPanel's example `lv_conf.h` usually already has them,
but confirm):

```c
#define LV_COLOR_DEPTH        16   // must match the RGB565 data in manpro_logo.c
#define LV_USE_FLEX             1
#define LV_FONT_MONTSERRAT_14   1
```

If `LV_COLOR_DEPTH` is set to anything other than `16`, the logo will render
with wrong/garbled colors — regenerate the asset instead of changing this.

## 2. Drop the files in

Copy all four files into your sketch folder (next to your `.ino`), or into
`src/` if you're using PlatformIO.

## 3. Call it from `setup()`

After your display + LVGL init (the part CrowPanel's example already gives
you) and after LVGL has ticked at least once:

```cpp
#include "manpro_splash.h"

void go_to_main_screen() {
  create_main_ui();   // whatever builds your normal attendance UI
}

void setup() {
  // ... your existing CrowPanel display/LVGL init ...

  manpro_show_splash(go_to_main_screen);
}

void loop() {
  lv_timer_handler();
  delay(5);
}
```

`manpro_show_splash()` loads its own full-screen `lv_obj_t`, runs the
animation, deletes itself, and calls `go_to_main_screen()` — you don't need to
manage the screen object yourself.

## If your panel isn't 800×480

The logo is sized at 480px wide, which reads well on an 800×480 landscape
panel. If your CrowPanel variant is a different resolution:

- Tell me the resolution and I'll re-export `manpro_logo.c` at the right size
  (don't just scale it in LVGL — re-export from the source PNG so it stays sharp).
- The layout code in `manpro_splash.cpp` uses `lv_obj_align(..., LV_ALIGN_CENTER/...)`
  relative to the screen, so it re-centers automatically either way.

## Memory footprint

The image asset is ~180KB, stored as a `const` array — the compiler places it
in flash (`.rodata`), not RAM, so it won't eat into your usable heap. ESP32S3
reads flash-mapped data directly, so LVGL can draw straight from it with no
extra copy step.
