#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/rtc_io.h>
#include "src/config.h"
#include "src/model/Types.h"
#include "src/display/EPD_5in0.h"
#include "src/display/GUI_Paint.h"
#include "src/display/fonts.h"
#include "src/display/DisplayManager.h"
#include "src/display/WaveshareRenderer.h"
#include "src/epub/Epub.h"
#include "src/epub/RubbishHtmlParser.h"
#include "src/storage/PositionStore.h"
#include "src/model/State.h"
#include "src/display/CoverArt.h"
#include "src/power/Battery.h"
#include "src/storage/SettingsStore.h"

size_t getArduinoLoopTaskStackSize() { return 32768; }

// =============================================================================
// Globals — only POD / trivial-constructor types live here.
// Epub*, RubbishHtmlParser*, WaveshareRenderer* are heap-allocated in setup().
// =============================================================================

DisplayManager    display;           // trivial constructor — safe as global
Battery           g_battery;          // trivial constructor — safe as global
UBYTE            *framebuf  = nullptr;
WaveshareRenderer *g_rend   = nullptr;
Epub              *g_epub   = nullptr;
RubbishHtmlParser *g_parser = nullptr;

EpubListState g_state;               // POD — zero-initialised in BSS

int g_spine_index   = 0;
int g_page_index    = 0;
int g_page_count    = 0;
int g_sel_epub      = 0;            // selected row in library screen
int g_menu_sel      = 0;            // selected row in menu
int g_settings_sel  = 0;            // selected row in settings screen
int g_toc_sel       = 0;            // selected row in table-of-contents screen

ReaderSettings g_settings;          // populated from SD in setup()/fast_resume()

AppState g_app_state = ST_LIBRARY;

unsigned long g_last_activity = 0;

// =============================================================================
// Deep-sleep state — survives power-off, lives in RTC slow memory
// =============================================================================

struct RtcSleepState {
    uint32_t magic;
    int32_t  app_state;
    int32_t  sel_epub;
    int32_t  spine_index;
    int32_t  page_index;
    int32_t  menu_sel;
};
RTC_DATA_ATTR static RtcSleepState rtc_sleep;
static const uint32_t SLEEP_MAGIC = 0x4B5A5376; // 'KZSv'

// =============================================================================
// Buttons  (active-low, internal pull-up)
// =============================================================================

// Btn and AppState enums are in Types.h (included above).

static void init_buttons() {
    pinMode(BTN_PREV,   INPUT_PULLUP);
    pinMode(BTN_NEXT,   INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
}

static Btn read_button() {
    static bool lp = false, ln = false, ls = false;
    static unsigned long quiet_until = 0;

    bool cp = digitalRead(BTN_PREV)   == LOW;
    bool cn = digitalRead(BTN_NEXT)   == LOW;
    bool cs = digitalRead(BTN_SELECT) == LOW;

    Btn b = BTN_NONE;
    if (millis() >= quiet_until) {
        if      (cp && !lp) b = BTN_LEFT;
        else if (cn && !ln) b = BTN_RIGHT;
        else if (cs && !ls) b = BTN_OK;
        if (b != BTN_NONE) quiet_until = millis() + 200;
    }

    lp = cp; ln = cn; ls = cs;
    return b;
}

// =============================================================================
// SD — scan for .epub files, add new ones to g_state
// =============================================================================

static void scan_for_epubs() {
    const char *dirs[] = { EPUB_DIR, "/" };
    for (auto dir : dirs) {
        File d = SD.open(dir);
        if (!d) continue;
        while (true) {
            File f = d.openNextFile();
            if (!f) break;
            String name = f.name();
            bool   is_dir = f.isDirectory();
            f.close();
            if (is_dir) continue;
            if (!name.endsWith(".epub") && !name.endsWith(".EPUB")) continue;

            char fp[MAX_PATH_SIZE];
            if (strcmp(dir, "/") == 0)
                snprintf(fp, sizeof(fp), "/sd/%s",    name.c_str());
            else
                snprintf(fp, sizeof(fp), "/sd%s/%s",  dir, name.c_str());

            bool found = false;
            for (int i = 0; i < g_state.num_epubs && !found; i++)
                found = strncmp(g_state.epub_list[i].path, fp, MAX_PATH_SIZE) == 0;
            if (found || g_state.num_epubs >= MAX_EPUB_LIST_SIZE) continue;

            EpubListItem &it = g_state.epub_list[g_state.num_epubs++];
            memset(&it, 0, sizeof(it));
            strncpy(it.path,  fp,           MAX_PATH_SIZE  - 1);
            strncpy(it.title, name.c_str(), MAX_TITLE_SIZE - 1);
        }
        d.close();
    }
}

// =============================================================================
// Position persistence
// =============================================================================

static void save_position() {
    if (!g_epub || g_sel_epub < 0 || g_sel_epub >= g_state.num_epubs) return;
    PositionStore::set_epub_position(
        g_state,
        g_state.epub_list[g_sel_epub].path,
        g_state.epub_list[g_sel_epub].title,
        (uint16_t)g_spine_index,
        (uint16_t)g_page_index,
        (uint16_t)g_page_count);
    PositionStore::save(g_state);
}

// =============================================================================
// Deep sleep
// =============================================================================

// Draws the currently open book's cover full-bleed (or a plain
// "Sleeping..." message if it has none), so it stays visible at zero power
// while the panel sleeps — leverages e-ink's bistable hold instead of
// clearing to blank white. No title text: the cover art already carries it.
// Only called when a book is actually open (see enter_deep_sleep(), which
// clears to white otherwise). Allocated from PSRAM rather than a static
// buffer — at full-screen size (~53KB) it's not worth permanently reserving
// for a screen only drawn once, right before a multi-hour sleep.
static void render_sleep_cover() {
    g_rend->clear_screen();

    uint8_t *cover_buf = (uint8_t *)ps_malloc(COVER_SLEEP_BYTES);
    bool have_cover = cover_buf && get_cover_sleep_bitmap(g_epub, cover_buf);

    if (have_cover) {
        int x = (g_rend->get_page_width()  - COVER_SLEEP_W) / 2;
        int y = (g_rend->get_page_height() - COVER_SLEEP_H) / 2;
        g_rend->draw_bitmap_1bpp(x, y, cover_buf, COVER_SLEEP_W, COVER_SLEEP_H);
    } else {
        const char *msg = "Sleeping...";
        int text_w = (int)strlen(msg) * g_rend->get_space_width();
        g_rend->draw_text((g_rend->get_page_width() - text_w) / 2, g_rend->get_page_height() / 2, msg);
    }
    free(cover_buf);

    display.showPage(framebuf, true);
}

static void enter_deep_sleep() {
    Serial.println("Idle: entering deep sleep");
    Serial.flush();

    // Snapshot navigation state into RTC memory
    rtc_sleep.magic       = SLEEP_MAGIC;
    rtc_sleep.app_state   = (int32_t)g_app_state;
    rtc_sleep.sel_epub    = (int32_t)g_sel_epub;
    rtc_sleep.spine_index = (int32_t)g_spine_index;
    rtc_sleep.page_index  = (int32_t)g_page_index;
    rtc_sleep.menu_sel    = (int32_t)g_menu_sel;

    // Configure wakeup on any button press (active-low → wake on LOW).
    // Requires Arduino ESP32 3.x / IDF 5.x for ESP_EXT1_WAKEUP_ANY_LOW.
    uint64_t wake_mask = (1ULL << BTN_PREV) | (1ULL << BTN_NEXT) | (1ULL << BTN_SELECT);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Enable RTC pull-ups so pins stay HIGH (not-pressed) while ESP32 is off.
    rtc_gpio_pullup_en((gpio_num_t)BTN_PREV);
    rtc_gpio_pullup_en((gpio_num_t)BTN_NEXT);
    rtc_gpio_pullup_en((gpio_num_t)BTN_SELECT);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_PREV);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_NEXT);
    rtc_gpio_pulldown_dis((gpio_num_t)BTN_SELECT);

    if (g_epub) {
        // A book is open — leave its cover on screen instead of blanking it;
        // e-ink holds the image with zero power through deep sleep.
        render_sleep_cover();
        display.sleep(false);
    } else {
        // No book open (e.g. sleeping from the library) — clear to white
        // rather than leave an arbitrary screen state burned in indefinitely.
        display.sleep(true);
    }

    esp_deep_sleep_start();
    // Never returns.
}

// Re-init after deep sleep wakeup without a full cold-boot scan.
static void fast_resume() {
    Serial.println("Waking from deep sleep...");

    // Framebuf in PSRAM
    UDOUBLE imgsize = ((EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1)) * EPD_H;
    framebuf = (UBYTE *)ps_malloc(imgsize);
    if (!framebuf) { Serial.println("FATAL: framebuf"); while (1); }

    // Re-init display registers (no panel clear — image already on screen)
    display.wake();
    Paint_NewImage(framebuf, EPD_W, EPD_H, 90, WHITE);

    // Renderer
    g_rend = new WaveshareRenderer(framebuf, EPD_H, EPD_W);
    if (!g_rend) { Serial.println("FATAL: renderer"); while (1); }

    // SD card
    static SPIClass sdSPI(HSPI);
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, sdSPI)) Serial.println("WARN: SD init failed on wake");

    // Buttons
    init_buttons();

    // Battery gauge (I2C/peripheral state doesn't survive deep sleep)
    g_battery.begin();

    // Settings (regular globals don't survive deep sleep either)
    SettingsStore::load(&g_settings);
    g_rend->set_large_font(g_settings.large_font);

    // Restore navigation state from RTC memory
    g_app_state   = (AppState)rtc_sleep.app_state;
    g_sel_epub    = rtc_sleep.sel_epub;
    g_spine_index = rtc_sleep.spine_index;
    g_page_index  = rtc_sleep.page_index;
    g_menu_sel    = rtc_sleep.menu_sel;
    // Settings/TOC aren't worth round-tripping through RTC memory — fall back to Menu
    if (g_app_state == ST_SETTINGS || g_app_state == ST_TOC) g_app_state = ST_MENU;

    // Load epub list from SD (needed for open_epub / render_library)
    PositionStore::load(&g_state);

    // Re-render the screen the user was on
    bool resumed = false;
    if ((g_app_state == ST_READER || g_app_state == ST_MENU)
        && g_sel_epub >= 0 && g_sel_epub < g_state.num_epubs) {
        g_epub = new Epub(g_state.epub_list[g_sel_epub].path);
        if (g_epub && g_epub->load()
            && load_spine_item(rtc_sleep.spine_index, rtc_sleep.page_index)) {
            render_current_page();
            display.showPage(framebuf, true);
            g_app_state = ST_READER;
            resumed = true;
        } else {
            delete g_epub; g_epub = nullptr;
            Serial.println("WARN: epub reload failed");
        }
    }

    if (!resumed) {
        scan_for_epubs();
        render_library();
        g_app_state = ST_LIBRARY;
    }

    // Let the wakeup button release before the main loop polls it
    delay(300);
    g_last_activity = millis();
}

// =============================================================================
// Spine / parser management
// =============================================================================

// Parse and layout spine[spine_index].  Does NOT replace g_parser on failure
// so the caller can try alternate spine indices without losing current content.
static bool load_spine_item(int spine_index, int page_index) {
    if (!g_epub) return false;
    if (spine_index < 0 || spine_index >= g_epub->get_spine_items_count()) return false;

    std::string path = g_epub->get_spine_item(spine_index);
    size_t html_sz = 0;
    uint8_t *html = g_epub->get_item_contents(path, &html_sz);
    if (!html) return false;

    size_t slash = path.rfind('/');
    std::string dir = slash != std::string::npos ? path.substr(0, slash + 1) : "";

    RubbishHtmlParser *np = new RubbishHtmlParser((const char *)html, (int)html_sz, dir);
    free(html);

    if (!np || !np->has_text_content()) { delete np; return false; }
    np->layout(g_rend, g_epub);
    int pc = np->get_page_count();
    if (pc == 0) { delete np; return false; }

    // Commit — only here do we touch the live g_parser
    delete g_parser;
    g_parser      = np;
    g_spine_index = spine_index;
    g_page_count  = pc;
    if (page_index >= pc) page_index = pc - 1;
    if (page_index <  0) page_index = 0;
    g_page_index  = page_index;
    return true;
}

// =============================================================================
// Screen renderers
// =============================================================================

// Chapter number (left) + battery icon/percentage (right), drawn in the top
// margin band — a blank buffer zone above the content area on every screen —
// so it never displaces text or affects pagination. Safe to call from any
// screen renderer. Chapter number only shows while a book is open (Reader,
// Menu, Settings, TOC); it's naturally absent on the Library screen since
// g_epub is null there.
static void draw_top_strip() {
    const int y = -26; // within the top margin band, above y=0 content

    if (g_epub) {
        char chapter[16];
        snprintf(chapter, sizeof(chapter), "Chapter %d", chapter_number_for_spine(g_epub, g_spine_index));
        g_rend->draw_text(0, y - 2, chapter);
    }

    if (!g_battery.available()) return;
    g_battery.update();

    const int icon_w = 32, icon_h = 16, nub_w = 3, nub_h = 8;
    const int x = g_rend->get_page_width() - icon_w - nub_w;

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", g_battery.percent());
    int text_w = (int)strlen(pct) * g_rend->get_space_width();
    g_rend->draw_text(x - text_w - 6, y - 2, pct);

    g_rend->draw_rect(x, y, icon_w, icon_h, 1);
    g_rend->fill_rect(x + icon_w, y + (icon_h - nub_h) / 2, nub_w, nub_h, 1);

    int fill_w = (icon_w - 4) * g_battery.percent() / 100;
    if (fill_w > 0)
        g_rend->fill_rect(x + 2, y + 2, fill_w, icon_h - 4, 1);
}

static void render_current_page() {
    if (!g_parser || !g_rend || !g_epub) return;
    g_rend->clear_screen();
    g_parser->render_page(g_page_index, g_rend, g_epub);
    draw_top_strip();
}

static void show_msg(const char *line1, const char *line2 = nullptr) {
    g_rend->clear_screen();
    int mid = g_rend->get_page_height() / 2 - g_rend->get_line_height();
    g_rend->draw_text(0, mid, line1);
    if (line2) g_rend->draw_text(0, mid + g_rend->get_line_height(), line2);
    display.showPage(framebuf, true);
}

// Library is a grid of book tiles (cover + title), GRID_COLS x GRID_ROWS per
// page, paginated by g_sel_epub / TILES_PER_PAGE — the same "page jumps when
// the cursor crosses its boundary" behaviour the old scrolling list had.
static const int GRID_COLS      = 2;
static const int GRID_ROWS      = 4;
static const int TILES_PER_PAGE = GRID_COLS * GRID_ROWS;
static const int TITLE_GAP      = 6; // space between a cover's bottom edge and its title line

// 1-based chapter number for a spine position — 0 before the first chapter
// (e.g. a title/copyright page preceding it), and the last chapter's number
// for any section past it, since get_toc_index_for_spine_index() already
// resolves to the most recent chapter boundary at or before spine_index.
static int chapter_number_for_spine(Epub *epub, int spine_index) {
    if (!epub) return 0;
    return epub->get_toc_index_for_spine_index(spine_index) + 1;
}

// Fetches (decoding + SD-caching as needed) and draws the cover for the book
// at `epub_idx` at (x, y), and refreshes its stored title from the epub's
// own metadata (scan_for_epubs() only ever seeds it from the filename). A
// throwaway Epub instance is enough — we only need its title/cover metadata,
// not a full spine/TOC parse for reading. Author/chapter aren't cached in
// EpubListItem (unlike title) since they're only surfaced for the currently
// selected book, not worth persisting/growing PositionStore's file format
// for — the out-params capture them when the caller wants them.
static void render_cover_thumbnail(int epub_idx, int x, int y,
                                   std::string *out_author = nullptr,
                                   int *out_chapter_num = nullptr) {
    static uint8_t cover_buf[COVER_THUMB_BYTES];
    if (epub_idx < 0 || epub_idx >= g_state.num_epubs) return;

    EpubListItem &it = g_state.epub_list[epub_idx];
    Epub cover_epub(it.path);
    if (!cover_epub.load()) return;

    const std::string &title = cover_epub.get_title();
    if (!title.empty()) {
        strncpy(it.title, title.c_str(), MAX_TITLE_SIZE - 1);
        it.title[MAX_TITLE_SIZE - 1] = '\0';
    }
    if (out_author) *out_author = cover_epub.get_author();
    if (out_chapter_num) *out_chapter_num = chapter_number_for_spine(&cover_epub, it.current_section);

    if (get_cover_thumbnail(&cover_epub, cover_buf))
        g_rend->draw_bitmap_1bpp(x, y, cover_buf, COVER_THUMB_W, COVER_THUMB_H);
}

static void render_library() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = 0;

    // Header
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "Pushkar's Library - %d books", g_state.num_epubs);
    g_rend->draw_text(0, y, hdr, true);
    y += lh + lh / 2;
    const int grid_top = y;

    if (g_state.num_epubs == 0) {
        g_rend->draw_text(0, y, "No books found.");  y += lh;
        g_rend->draw_text(0, y, "Copy .epub files to /epub on SD card.");
        draw_top_strip();
        display.showPage(framebuf, true);
        return;
    }

    const int tile_w = g_rend->get_page_width() / GRID_COLS;
    // Content-sized, not stretched to fill the available grid height — the
    // panel's logical canvas is portrait (rotated 90 degrees), so it has far
    // more vertical room than a 2-row grid needs; stretching tiles to fill it
    // just opens a big gap between rows instead of showing more books.
    const int tile_h = COVER_THUMB_H + TITLE_GAP + lh;
    const int max_title_chars = max(4, tile_w / g_rend->get_space_width() - 1);

    int page  = g_sel_epub / TILES_PER_PAGE;
    int first = page * TILES_PER_PAGE;
    std::string sel_author;
    int sel_chapter_num = 0;

    for (int slot = 0; slot < TILES_PER_PAGE; slot++) {
        int idx = first + slot;
        if (idx >= g_state.num_epubs) break;

        int col = slot % GRID_COLS;
        int row = slot / GRID_COLS;
        int tile_x = col * tile_w;
        int tile_y = grid_top + row * tile_h;
        int cover_x = tile_x + (tile_w - COVER_THUMB_W) / 2;

        render_cover_thumbnail(idx, cover_x, tile_y,
                              idx == g_sel_epub ? &sel_author      : nullptr,
                              idx == g_sel_epub ? &sel_chapter_num : nullptr);

        if (idx == g_sel_epub)
            g_rend->draw_rect(cover_x - 4, tile_y - 4, COVER_THUMB_W + 8, COVER_THUMB_H + 8, 1);

        const EpubListItem &it = g_state.epub_list[idx];
        char title[48];
        int  n = (int)strlen(it.title);
        if (n > max_title_chars) n = max_title_chars;
        strncpy(title, it.title, n);
        title[n] = '\0';
        // Not bold: get_space_width() below (used for centering) is always
        // Font20's width, which would under-measure a bold (Font24) title.
        g_rend->draw_text(tile_x + (tile_w - (int)strlen(title) * g_rend->get_space_width()) / 2,
                          tile_y + COVER_THUMB_H + TITLE_GAP, title);
    }

    // Progress for currently selected book — folding author into this
    // existing line (rather than adding a new one) since the grid already
    // uses its full vertical budget across 4 rows. Flush-left, matching the
    // header above it — no leading indent (that was a leftover cursor-prefix
    // alignment from the old list layout, not a grid).
    const EpubListItem &sel = g_state.epub_list[g_sel_epub];
    char prog[80] = "";
    int  off = 0;
    if (!sel_author.empty()) {
        off = snprintf(prog, sizeof(prog), "%s  ", sel_author.c_str());
        // snprintf returns the length it *would* have written; clamp so a
        // long author name can't push the offset past the buffer.
        if (off < 0) off = 0;
        if (off > (int)sizeof(prog) - 1) off = (int)sizeof(prog) - 1;
    }
    if (sel.pages_in_current_section > 0)
        snprintf(prog + off, sizeof(prog) - off, "Chapter %d  Page %d/%d",
                 sel_chapter_num, sel.current_page + 1, sel.pages_in_current_section);
    else
        snprintf(prog + off, sizeof(prog) - off, "(not yet opened)");
    g_rend->draw_text(0, g_rend->get_page_height() - lh, prog);
    draw_top_strip();
    display.showPage(framebuf, true);
}

static const int MENU_ITEM_COUNT = 5;

static void render_menu() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = g_rend->get_page_height() / 2 - lh * (MENU_ITEM_COUNT + 1);

    g_rend->draw_text(0, y, "MENU", true);
    y += lh + lh / 2;

    const char *opts[] = { "Continue Reading", "Go to Library", "Table of Contents", "Settings", "Sleep" };
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", (i == g_menu_sel) ? ">" : " ", opts[i]);
        g_rend->draw_text(0, y, line, i == g_menu_sel);
        y += lh;
    }
    draw_top_strip();
    display.showPage(framebuf, true);
}

// =============================================================================
// Open epub
// =============================================================================

static bool open_epub(int epub_idx) {
    if (epub_idx < 0 || epub_idx >= g_state.num_epubs) return false;
    EpubListItem &it = g_state.epub_list[epub_idx];

    show_msg("Loading...");

    delete g_parser; g_parser = nullptr;
    delete g_epub;   g_epub   = nullptr;

    g_epub = new Epub(it.path);
    if (!g_epub || !g_epub->load()) {
        Serial.printf("Failed to open %s\n", it.path);
        delete g_epub; g_epub = nullptr;
        return false;
    }

    // Update stored title with the real one from epub metadata
    const std::string &t = g_epub->get_title();
    if (!t.empty()) {
        strncpy(it.title, t.c_str(), MAX_TITLE_SIZE - 1);
        it.title[MAX_TITLE_SIZE - 1] = '\0';
    }

    g_sel_epub = epub_idx;

    // Try saved position; scan forward if it fails
    bool ok = (it.current_section < (uint16_t)g_epub->get_spine_items_count())
              && load_spine_item(it.current_section, it.current_page);
    if (!ok) {
        int n = g_epub->get_spine_items_count();
        for (int si = 0; si < n && !ok; si++) ok = load_spine_item(si, 0);
    }
    if (!ok) {
        delete g_epub; g_epub = nullptr;
        return false;
    }
    return true;
}

// =============================================================================
// Page navigation
// =============================================================================

static void next_page() {
    if (!g_parser || !g_epub) return;

    if (g_page_index < g_page_count - 1) {
        g_page_index++;
        render_current_page();
        save_position();
        display.showPage(framebuf, true);
        return;
    }

    // Cross spine boundary forward
    show_msg("Loading...");
    int n = g_epub->get_spine_items_count();
    for (int si = g_spine_index + 1; si < n; si++) {
        if (load_spine_item(si, 0)) {
            render_current_page();
            save_position();
            display.showPage(framebuf, true);
            return;
        }
    }
    show_msg("End of book.", "SELECT: menu");
}

static void prev_page() {
    if (!g_parser || !g_epub) return;

    if (g_page_index > 0) {
        g_page_index--;
        render_current_page();
        save_position();
        display.showPage(framebuf, true);
        return;
    }

    // Cross spine boundary backward
    show_msg("Loading...");
    for (int si = g_spine_index - 1; si >= 0; si--) {
        if (load_spine_item(si, 0)) {
            g_page_index = g_page_count - 1;
            render_current_page();
            save_position();
            display.showPage(framebuf, true);
            return;
        }
    }
    // Already at the very first page — just redisplay
    render_current_page();
    display.showPage(framebuf, true);
}

// =============================================================================
// Input handlers
// =============================================================================

static void handle_library(Btn b) {
    if (b == BTN_LEFT) {
        if (g_sel_epub > 0) { g_sel_epub--; render_library(); }
    } else if (b == BTN_RIGHT) {
        if (g_sel_epub < g_state.num_epubs - 1) { g_sel_epub++; render_library(); }
    } else if (b == BTN_OK) {
        if (g_state.num_epubs == 0) {
            scan_for_epubs();
            render_library();
        } else if (open_epub(g_sel_epub)) {
            render_current_page();
            display.showPage(framebuf, true);
            g_app_state = ST_READER;
        } else {
            show_msg("Failed to open book.");
            delay(2000);
            render_library();
        }
    }
}

static void handle_reader(Btn b) {
    if      (b == BTN_LEFT)  prev_page();
    else if (b == BTN_RIGHT) next_page();
    else if (b == BTN_OK) {
        g_menu_sel = 0;
        render_menu();
        g_app_state = ST_MENU;
    }
}

static void handle_menu(Btn b) {
    if (b == BTN_LEFT) {
        if (g_menu_sel > 0) { g_menu_sel--; render_menu(); }
    } else if (b == BTN_RIGHT) {
        if (g_menu_sel < MENU_ITEM_COUNT - 1) { g_menu_sel++; render_menu(); }
    } else if (b == BTN_OK) {
        if (g_menu_sel == 0) {
            // Continue Reading
            render_current_page();
            display.showPage(framebuf, true);
            g_app_state = ST_READER;
        } else if (g_menu_sel == 1) {
            // Go to Library
            delete g_parser; g_parser = nullptr;
            delete g_epub;   g_epub   = nullptr;
            g_sel_epub = 0;
            render_library();
            g_app_state = ST_LIBRARY;
        } else if (g_menu_sel == 2) {
            // Table of Contents — default the cursor to the first chapter
            // (index 1; Back is index 0), not to Back itself.
            g_toc_sel = (g_epub && g_epub->get_toc_items_count() > 0) ? 1 : 0;
            render_toc();
            g_app_state = ST_TOC;
        } else if (g_menu_sel == 3) {
            // Settings
            g_settings_sel = 0;
            render_settings();
            g_app_state = ST_SETTINGS;
        } else {
            // Sleep
            save_position();
            enter_deep_sleep();  // never returns
        }
    }
}

// =============================================================================
// Settings
// =============================================================================

static const unsigned long SLEEP_TIMEOUT_PRESETS_MIN[] = { 1, 3, 5, 10, 30 };
static const int SLEEP_TIMEOUT_PRESET_COUNT =
    sizeof(SLEEP_TIMEOUT_PRESETS_MIN) / sizeof(SLEEP_TIMEOUT_PRESETS_MIN[0]);
static const int SETTINGS_ITEM_COUNT = 4; // Sleep Timeout, Font Size, Battery, Back

static void render_settings() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = 0;

    g_rend->draw_text(0, y, "SETTINGS", true);
    y += lh + lh / 2;

    char line[48];

    snprintf(line, sizeof(line), "%s Sleep Timeout: %lu min",
             g_settings_sel == 0 ? ">" : " ", g_settings.sleep_timeout_ms / 60000UL);
    g_rend->draw_text(0, y, line, g_settings_sel == 0);
    y += lh;

    snprintf(line, sizeof(line), "%s Font Size: %s",
             g_settings_sel == 1 ? ">" : " ", g_settings.large_font ? "Large" : "Regular");
    g_rend->draw_text(0, y, line, g_settings_sel == 1);
    y += lh;

    if (g_battery.available())
        snprintf(line, sizeof(line), "%s Battery: %.3fV  %d%%",
                 g_settings_sel == 2 ? ">" : " ", g_battery.voltage(), g_battery.percent());
    else
        snprintf(line, sizeof(line), "%s Battery: unavailable", g_settings_sel == 2 ? ">" : " ");
    g_rend->draw_text(0, y, line, g_settings_sel == 2);
    y += lh;

    snprintf(line, sizeof(line), "%s Back", g_settings_sel == 3 ? ">" : " ");
    g_rend->draw_text(0, y, line, g_settings_sel == 3);

    draw_top_strip();
    display.showPage(framebuf, true);
}

static void handle_settings(Btn b) {
    if (b == BTN_LEFT) {
        if (g_settings_sel > 0) { g_settings_sel--; render_settings(); }
        return;
    }
    if (b == BTN_RIGHT) {
        if (g_settings_sel < SETTINGS_ITEM_COUNT - 1) { g_settings_sel++; render_settings(); }
        return;
    }
    if (b != BTN_OK) return;

    if (g_settings_sel == 0) {
        // Cycle sleep timeout through the preset list
        unsigned long current_min = g_settings.sleep_timeout_ms / 60000UL;
        int idx = 0;
        for (int i = 0; i < SLEEP_TIMEOUT_PRESET_COUNT; i++)
            if (SLEEP_TIMEOUT_PRESETS_MIN[i] == current_min) { idx = i; break; }
        idx = (idx + 1) % SLEEP_TIMEOUT_PRESET_COUNT;
        g_settings.sleep_timeout_ms = SLEEP_TIMEOUT_PRESETS_MIN[idx] * 60000UL;
        SettingsStore::save(g_settings);
        render_settings();
    } else if (g_settings_sel == 1) {
        // Toggle font size. Reused Font24 (not a real bold face) means real
        // bold/emphasis spans stop standing out while this is on — an
        // accepted trade-off rather than adding a third font table.
        g_settings.large_font = !g_settings.large_font;
        g_rend->set_large_font(g_settings.large_font);
        SettingsStore::save(g_settings);
        // Layout was computed at the old font metrics — re-layout the
        // current chapter so pagination matches what will actually render.
        if (g_epub && g_parser) load_spine_item(g_spine_index, 0);
        render_settings();
    } else if (g_settings_sel == 3) {
        // Back
        render_menu();
        g_app_state = ST_MENU;
    }
    // g_settings_sel == 2 (Battery) is informational only — OK does nothing.
}

// =============================================================================
// Table of Contents
// =============================================================================

static void render_toc() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = 0;

    g_rend->draw_text(0, y, "TABLE OF CONTENTS", true);
    y += lh + lh / 2;

    int toc_count   = g_epub ? g_epub->get_toc_items_count() : 0;
    int total_items = toc_count + 1; // + synthetic "Back" row at index 0

    if (toc_count == 0) {
        g_rend->draw_text(0, y, "No table of contents available.");
        y += lh + lh / 2;
        g_rend->draw_text(0, y, "SELECT: back");
        draw_top_strip();
        display.showPage(framebuf, true);
        return;
    }

    const int footer_lines = 1;
    const int avail   = g_rend->get_page_height() - y - lh * footer_lines;
    const int max_vis = avail / lh;
    const int max_chars = max(4, g_rend->get_page_width() / g_rend->get_space_width() - 2);

    int scroll = g_toc_sel - max_vis / 2;
    if (scroll < 0) scroll = 0;
    if (scroll > total_items - max_vis) scroll = max(0, total_items - max_vis);

    for (int i = scroll; i < total_items && y + lh <= g_rend->get_page_height() - lh * footer_lines; i++) {
        bool sel = (i == g_toc_sel);
        char line[64];

        if (i == 0) {
            snprintf(line, sizeof(line), "%s Back", sel ? ">" : " ");
        } else {
            char title[48];
            const std::string &full = g_epub->get_toc_item(i - 1).title;
            int n = (int)full.size();
            if (n > max_chars) n = max_chars;
            strncpy(title, full.c_str(), n);
            title[n] = '\0';
            snprintf(line, sizeof(line), "%s %s", sel ? ">" : " ", title);
        }
        g_rend->draw_text(0, y, line, sel);
        y += lh;
    }

    draw_top_strip();
    display.showPage(framebuf, true);
}

static void handle_toc(Btn b) {
    int toc_count   = g_epub ? g_epub->get_toc_items_count() : 0;
    int total_items = toc_count + 1; // + synthetic "Back" row at index 0

    if (b == BTN_LEFT) {
        if (g_toc_sel > 0) { g_toc_sel--; render_toc(); }
    } else if (b == BTN_RIGHT) {
        if (g_toc_sel < total_items - 1) { g_toc_sel++; render_toc(); }
    } else if (b == BTN_OK) {
        if (g_toc_sel == 0) {
            // Back
            render_menu();
            g_app_state = ST_MENU;
            return;
        }
        // Jump to the chapter's spine item, first page. The TOC entry's
        // in-page anchor (if any) isn't tracked here — landing on the
        // chapter's first page is a reasonable simplification given this
        // reader paginates per spine item, not per anchor.
        int spine_index = g_epub->get_spine_index_for_toc_index(g_toc_sel - 1);
        show_msg("Loading...");
        if (load_spine_item(spine_index, 0)) {
            save_position();
            render_current_page();
            display.showPage(framebuf, true);
            g_app_state = ST_READER;
        } else {
            show_msg("Failed to load chapter.");
            delay(1500);
            render_toc();
        }
    }
}

// =============================================================================
// Arduino entry points
// =============================================================================

// Reported at every boot so a reset while running on battery is
// distinguishable from a genuine power failure — e.g. BROWNOUT means the
// supply sagged under load and the chip reset (the e-ink panel just keeps
// showing its last frame through that, looking like the device "stopped").
static const char *reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software reset";
        case ESP_RST_PANIC:     return "panic/exception";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "woke from deep sleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (supply sagged)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

void setup() {
    Serial.begin(115200);
    Serial.printf("Reset reason: %s\n", reset_reason_str(esp_reset_reason()));

    // Fast path: wake from deep sleep
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
        && rtc_sleep.magic == SLEEP_MAGIC) {
        fast_resume();
        return;
    }

    delay(1000);
    Serial.println("Katze cold boot...");

    // Framebuf in PSRAM
    UDOUBLE imgsize = ((EPD_W % 8 == 0) ? (EPD_W / 8) : (EPD_W / 8 + 1)) * EPD_H;
    framebuf = (UBYTE *)ps_malloc(imgsize);
    if (!framebuf) { Serial.println("FATAL: framebuf alloc"); while (1); }

    // Display
    display.begin();
    Paint_NewImage(framebuf, EPD_W, EPD_H, 90, WHITE);
    Paint_Clear(WHITE);

    // Renderer — must be after Paint_NewImage configures the global Paint struct
    g_rend = new WaveshareRenderer(framebuf, EPD_H, EPD_W);
    if (!g_rend) { Serial.println("FATAL: renderer alloc"); while (1); }

    // SD card
    static SPIClass sdSPI(HSPI);
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, sdSPI)) { Serial.println("FATAL: SD"); while (1); }

    // Buttons
    init_buttons();

    // Battery gauge
    g_battery.begin();

    // Settings
    SettingsStore::load(&g_settings);
    g_rend->set_large_font(g_settings.large_font);

    // Load persisted state and scan for new epubs
    PositionStore::load(&g_state);
    scan_for_epubs();
    if (g_state.num_epubs > 0) PositionStore::save(g_state);

    Serial.printf("Found %d epub(s)\n", g_state.num_epubs);
    g_last_activity = millis();

    // Auto-open the only book if it has been read before
    if (g_state.num_epubs == 1 && g_state.epub_list[0].pages_in_current_section > 0) {
        if (open_epub(0)) {
            render_current_page();
            display.showPage(framebuf, true);
            g_app_state = ST_READER;
            return;
        }
    }

    // Default: library screen
    render_library();
    g_app_state = ST_LIBRARY;
}

void loop() {
    Btn b = read_button();
    if (b != BTN_NONE) {
        Serial.printf("Button: %d, state: %d\n", (int)b, (int)g_app_state);
        g_last_activity = millis();
        switch (g_app_state) {
            case ST_LIBRARY:  handle_library(b);  break;
            case ST_READER:   handle_reader(b);   break;
            case ST_MENU:     handle_menu(b);     break;
            case ST_SETTINGS: handle_settings(b); break;
            case ST_TOC:      handle_toc(b);      break;
        }
        Serial.println("Button: handler returned");
    }

    // Idle timeout — save position then deep sleep
    if (millis() - g_last_activity > g_settings.sleep_timeout_ms) {
        save_position();
        enter_deep_sleep();  // never returns
    }

    yield();
    delay(20);  // ~50 Hz button polling
}
