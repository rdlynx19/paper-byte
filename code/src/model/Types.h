#pragma once

// Button press values returned by read_button().
// Named BTN_LEFT/RIGHT/OK to avoid collision with the BTN_PREV/NEXT/SELECT
// pin-number macros defined in config.h. BTN_OK_LONG is a held-SELECT
// gesture (fires once, while still held, past a threshold) — the only way
// to reach a "second" action off the SELECT button on a 3-button device;
// see read_button() for why BTN_OK itself now fires on release rather than
// on press.
enum Btn { BTN_NONE, BTN_LEFT, BTN_RIGHT, BTN_OK, BTN_OK_LONG };

// App state machine states.
enum AppState { ST_LIBRARY, ST_READER, ST_MENU, ST_SETTINGS, ST_TOC, ST_LIBRARY_MENU, ST_FILE_TRANSFER };
