# Paper Byte

An ESP32-S3 e-reader, built from scratch: custom PCB, 3D-printed case, and firmware that unzips and renders EPUBs on a 5" e-paper display.

Full write-up (design story, photos, schematics): https://pushkardave.com/paper-byte

## What it does

Paper Byte parses EPUB files straight off an SD card and renders them on an e-ink screen, with three-button navigation (prev / next / select) driving a small state machine: Library, Reader, Menu, Settings, Table of Contents, and a Wi-Fi file transfer mode.

- Unzips and parses EPUBs (XHTML + metadata) with no external OS or filesystem — just the ESP32 and an SD card
- Reflowable text layout: word-wrap, adjustable font size, margins
- Cover art extraction and a library grid with cached thumbnails
- Table of contents / chapter jump
- Persistent reading position per book, survives power loss
- Battery percentage from a MAX17048 fuel gauge, not voltage-guessing
- Sleeps to the book's cover at zero power (e-ink is bistable) and wakes straight back into the reader
- Wireless library management: the device hosts its own Wi-Fi access point and a small HTTP page to upload/delete EPUBs, since the SD card isn't reachable once the case is closed

## Hardware

| Component | Role |
|---|---|
| Adafruit ESP32-S3 Feather | Parses EPUBs, drives the display, runs the whole app |
| Waveshare 5" E-Paper Display + driver HAT | Renders pages over SPI |
| Adafruit SD Card module | EPUB storage, hot-swappable (card-detect pin) |
| MAX17048 fuel gauge (on the Feather) | Battery % over I2C |
| 3 tactile switches | Prev / Next / Select |
| 1S LiPo battery | Power, charged over the Feather's USB-C |

A custom through-hole PCB (`pcb/`) ties the Feather, SD module, and e-paper HAT together on shared SPI buses, eliminating almost all point-to-point wiring. The 3D-printed enclosure is in `cad/`.

The full design story — including an earlier Raspberry Pi Zero 2W attempt that got abandoned for being too clunky to fit in a case — is in the [write-up](https://pushkardave.com/paper-byte).

## Software architecture

```
EPUB (zip) -> unzip/parse chapter -> word-wrap into pages sized to the display -> render to e-paper
```

- Each chapter is parsed and laid out once, then cached in RAM — flipping pages just re-renders from the cache; only crossing into a new chapter costs a re-parse.
- Reading position, settings, and cover art are all cached to the SD card.

### Code layout (`code/src/`)

- `epub/` — ZIP extraction (`miniz`), XHTML parsing (`tinyxml2` + a small "rubbish" HTML parser), EPUB metadata
- `model/` — page/block layout model that turns parsed HTML into fixed-size pages
- `display/` — e-paper driver (`EPD_5in0`), drawing primitives (`GUI_Paint`), bitmap fonts, cover art rendering
- `storage/` — SD-backed reading position and settings persistence
- `power/` — MAX17048 battery fuel gauge wrapper
- `network/` — `FileServer`, the AP-mode Wi-Fi file manager

## Building / flashing

Arduino IDE or `arduino-cli`, targeting:

```
esp32:esp32:adafruit_feather_esp32s3:PartitionScheme=huge_app
```

Required libraries (beyond the ESP32 Arduino core, which already includes `WiFi`, `WebServer`, `SD`, `SPI`, `Wire`):

- `Adafruit_MAX1704X` (battery fuel gauge)

Open `code/code.ino`, select the board/partition scheme above, and flash.

## Wireless file manager

From the Library menu, entering File Transfer mode starts a Wi-Fi access point (SSID/password in `code/src/config.h`) and a small web page for uploading/deleting EPUBs — no need to open the case to manage books. Idle-timeout sleep is suspended while the mode is active; exiting tears down Wi-Fi and re-scans the library.

## Status / roadmap

Actively developed. Notably still missing:
- Partial e-ink refresh for page turns (currently a full refresh every page)
- Bookmarks
- A dedicated low-battery/charging screen

See `esp32-eink-reader-features.md` for the full feature checklist and `wifi-file-manager-plan.md` for the file-manager design doc.

## License

GPL-3.0 — see [LICENSE](LICENSE).
