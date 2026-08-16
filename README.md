# Paper Byte

Paper Byte is ESP32-S3 based e-reader,
This repository contains the PCB design files, the CAD files for the case, and the firmware to read and parse EPUB files. 

For a more detailed guide, visit my blog post - [PaperByte](https://pushkardave.com/paper-byte)

## Overview

Paper Byte parses EPUB files straight off an SD card and renders them on an e-ink screen, with three-button navigation (prev / next / select) driving a small state machine: Library, Reader, Menu, Settings, Table of Contents, and a Wi-Fi file transfer mode.

- Unzips and parses EPUBs (XHTML + metadata) from an SD card
- Reflowable text layout: word-wrap, adjustable font size, margins
- Cover art extraction and a library grid with cached thumbnails
- Table of contents / chapter jump
- Persistent reading position per book, survives power loss
- Battery percentage from a MAX17048 fuel gauge
- Sleeps to the book's cover at zero power and wakes straight back into the reader
- Wireless library management: the device hosts its own Wi-Fi access point and a small HTTP page to upload/delete EPUBs

## Hardware

| Component | Role |
|---|---|
| Adafruit ESP32-S3 Feather | Parses EPUBs, drives the display, runs the whole app |
| Waveshare 5" E-Paper Display + driver HAT | Renders pages over SPI |
| Adafruit SD Card module | EPUB storage, hot-swappable (card-detect pin) |
| MAX17048 fuel gauge (on the Feather) | Battery % over I2C |
| 3 tactile switches | Prev / Next / Select |
| 1S LiPo battery | Power, charged over the Feather's USB-C |

A custom through-hole PCB (`pcb/`) ties the Feather, SD module, and e-paper HAT together on SPI buses. The 3D-printed enclosure is in `cad/`.

## Software Architecture

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


## Wireless File Manager

From the Library menu, entering File Transfer mode starts a Wi-Fi access point (SSID/password in `code/src/config.h`) and a small web page for uploading/deleting EPUBs. Idle-timeout sleep is suspended while the mode is active; exiting tears down Wi-Fi and re-scans the library.

## Future Work

- Partial e-ink refresh for page turns (currently a full refresh every page)
- Bookmarks
- A dedicated low-battery/charging screen
