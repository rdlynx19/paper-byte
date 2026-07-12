# Wireless File Manager — Plan

## Why

The PCB has an SD card module, but once it's in an enclosure the card
almost certainly won't be physically accessible without opening the case.
Adding/removing epub files needs a wireless path instead.

## Platform facts (verified against the installed toolchain, not assumed)

- **Bluetooth is not a viable transport.** The Adafruit ESP32-S3 Feather's
  sdkconfig has `CONFIG_BT_BLE_ENABLED=y` but no Classic Bluetooth (no SPP —
  `esp_spp_api.h`, which `BluetoothSerial` needs, isn't enabled on this
  target). Only BLE is available, and BLE's throughput (tens of KB/s at
  best) is a poor fit for multi-megabyte epub files. **WiFi is the only
  realistic option.**
- **No new libraries needed.** `WiFi.h`, `WebServer.h` (synchronous HTTP
  server), and `DNSServer.h` all ship with the ESP32 Arduino core already
  installed — nothing new to pull in.

## Chosen approach

A small on-demand HTTP file-manager, served by the ESP32 itself:

1. User picks a menu option to enter **File Transfer mode**.
2. The device starts a WiFi **access point** (its own network — no
   dependency on a home network being in range, and no need to type a WiFi
   password on a 3-button device).
3. Screen shows the AP's SSID + a fixed URL (e.g. `http://192.168.4.1`).
4. User connects their phone/laptop to that network, opens the URL in a
   browser, and gets a plain page: list of epub files (name + size) each
   with a delete link, plus a file-upload form to add new ones.
5. User exits (button press) — WiFi and the web server shut down, the
   library re-scans to pick up changes, and normal operation (including
   idle-timeout sleep) resumes.

### Why AP mode instead of joining the home WiFi

Joining an existing network needs credentials entered somewhere. With only
PREV/NEXT/SELECT and no keyboard, on-device text entry is painful. AP mode
needs zero configuration and always works, at the cost of the connecting
phone briefly losing its other internet connection while attached (correct
trade-off for how infrequently this runs). **Recommended for v1.**

A phase-2 option: read SSID/password from a small text file already sitting
on the SD card (placed there once, from a computer, at the same time books
are loaded via a laptop before the case is closed) and join that network
if reachable, falling back to AP mode otherwise. This keeps the phone's own
internet connection alive during transfers, at the cost of needing that
one-time setup file. Not needed for v1; flagged here so it isn't forgotten.

### Why a plain HTTP page instead of WebDAV/FTP

WebDAV or FTP would let a phone/computer mount the SD card as a normal
network drive (drag-and-drop, no custom UI) — nicer in principle, but ESP32
support for either is less mature/more code than a synchronous `WebServer`
page, for a feature that's used rarely and doesn't need to feel like a
native OS integration. A basic list+upload+delete web page is simpler to
build correctly and reason about.

## On-device UX

- **Entry point**: needs a decision — there's no existing free button
  gesture on the Library screen (LEFT/RIGHT navigate, OK opens/rescans),
  and this needs to be reachable *without* a book being open (an empty
  library, e.g. first-time setup, is exactly when this matters most).
  Options: a long-press on SELECT from the Library screen, or a new item
  in a to-be-created "Library menu" (mirroring the Reader's Menu screen).
  **Needs your input before implementation — see Open Questions.**
- **Active screen**: SSID, URL, a short "connect your phone, then visit
  this address" message, and a clear "press X to stop" hint. No page
  turning/library browsing while active — this is a modal-like mode.
- **Idle-timeout sleep must be suspended** while this mode is active (don't
  want the device to deep-sleep mid-upload).
- **On exit**: tear down WiFi fully (`WiFi.mode(WIFI_OFF)`), re-run
  `scan_for_epubs()`, and return to the library screen.

## Web page behavior

- `GET /` — list of `.epub` files in `/epub` (name, size), each with a
  delete link; an upload form (`<input type=file multiple>`).
- `POST /upload` — streamed multipart write straight to SD (not buffered
  fully in RAM first — epub files can be several MB), added to `/epub/`.
- `POST /delete` — removes the file from SD, **and** cleans up anything
  keyed to it: its `PositionStore` entry (reading position) and its cached
  cover thumbnails (`/sd/cov2_*.bin` — currently keyed by a hash of the
  epub's path, so deleting the source file orphans its cache entries
  unless we clean them up explicitly).
- Bare HTML, no JS framework — this only needs to work in a phone browser
  for a couple of minutes at a time.

## Integration points with the existing app

- New module, e.g. `src/network/FileServer.h/.cpp` — mirrors the existing
  style (`Battery`, `SettingsStore` etc.): a class wrapping WiFi + WebServer
  lifecycle (`begin()`/`update()`/`stop()`), kept out of `code.ino`'s
  direct control flow as much as possible.
- New `AppState` (e.g. `ST_FILE_TRANSFER`), following the same pattern as
  `ST_SETTINGS`/`ST_TOC`.
- `loop()` needs to keep calling the file server's request-handling method
  while in this state, instead of (or alongside) button polling.
- Deleting the currently-open book while file-transfer mode is active is an
  edge case worth deciding on deliberately rather than leaving undefined
  (unlikely in practice, since you can't read while in this mode, but a
  book could still be "open" in the `g_epub` sense from before you entered
  it).

## Security

No authentication planned for v1 — the AP itself requires its own WiFi
password to join (already a basic gate), and this is a personal device.
HTTP basic auth on top is a cheap addition later if wanted, not core to
the plan.

## Implementation phases

1. **Core plumbing**: `FileServer` module, AP start/stop, new `AppState`,
   menu entry point (pending the Open Questions decision below), idle-sleep
   suspension while active.
2. **Web page**: list + delete.
3. **Upload**: streamed multipart write to SD, with library re-scan on exit.
4. **Cleanup on delete**: position entry + cached cover thumbnail(s).
5. *(Optional, later)* STA mode via an SD-based WiFi-credentials file, as
   a fallback-to-AP alternative.

## Open questions (need your call before implementation starts)

1. **Entry point UX**: long-press SELECT on the Library screen, or a new
   "Library menu" screen? The former needs new hold-detection logic in
   `read_button()`; the latter is a small new screen but reuses the
   existing menu pattern.
2. **AP-only for v1, or worth building the SD-credentials STA fallback now
   too?** (Recommend AP-only first, revisit STA if AP mode proves annoying
   in practice.)
3. Any preference on the AP's SSID/password, or fine with something generic
   like `Katze-Reader` / a fixed password baked into `config.h`?
