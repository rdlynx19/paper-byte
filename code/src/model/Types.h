#pragma once

// Button press values returned by read_button().
// Named BTN_LEFT/RIGHT/OK to avoid collision with the BTN_PREV/NEXT/SELECT
// pin-number macros defined in config.h.
enum Btn { BTN_NONE, BTN_LEFT, BTN_RIGHT, BTN_OK };

// App state machine states.
enum AppState { ST_LIBRARY, ST_READER, ST_MENU, ST_SETTINGS, ST_TOC };
