#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
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

size_t getArduinoLoopTaskStackSize() { return 32768; }

// =============================================================================
// Globals — only POD / trivial-constructor types live here.
// Epub*, RubbishHtmlParser*, WaveshareRenderer* are heap-allocated in setup().
// =============================================================================

DisplayManager    display;           // trivial constructor — safe as global
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

    // Clear panel to white before sleeping (prevents ghosting / long-term image burn).
    display.sleep();

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

    // Restore navigation state from RTC memory
    g_app_state   = (AppState)rtc_sleep.app_state;
    g_sel_epub    = rtc_sleep.sel_epub;
    g_spine_index = rtc_sleep.spine_index;
    g_page_index  = rtc_sleep.page_index;
    g_menu_sel    = rtc_sleep.menu_sel;

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

static void render_current_page() {
    if (!g_parser || !g_rend || !g_epub) return;
    g_rend->clear_screen();
    g_parser->render_page(g_page_index, g_rend, g_epub);
}

static void show_msg(const char *line1, const char *line2 = nullptr) {
    g_rend->clear_screen();
    int mid = g_rend->get_page_height() / 2 - g_rend->get_line_height();
    g_rend->draw_text(0, mid, line1);
    if (line2) g_rend->draw_text(0, mid + g_rend->get_line_height(), line2);
    display.showPage(framebuf, true);
}

// Cover thumbnail occupies a fixed strip on the left; everything else
// (header, list, progress, footer) starts to the right of it.
static const int COVER_GAP = 20;
static const int TEXT_X    = COVER_THUMB_W + COVER_GAP;

// Fetches (decoding + SD-caching as needed) and draws the cover for the book
// at `epub_idx`. A throwaway Epub instance is enough — we only need its
// title/cover metadata, not a full spine/TOC parse for reading.
static void render_cover_thumbnail(int epub_idx) {
    static uint8_t cover_buf[COVER_THUMB_BYTES];
    if (epub_idx < 0 || epub_idx >= g_state.num_epubs) return;

    Epub cover_epub(g_state.epub_list[epub_idx].path);
    if (cover_epub.load() && get_cover_thumbnail(&cover_epub, cover_buf))
        g_rend->draw_bitmap_1bpp(0, 0, cover_buf, COVER_THUMB_W, COVER_THUMB_H);
}

static void render_library() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = 0;

    // Header
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "LIBRARY  (%d books)", g_state.num_epubs);
    g_rend->draw_text(TEXT_X, y, hdr, true);
    y += lh + lh / 2;

    if (g_state.num_epubs == 0) {
        g_rend->draw_text(TEXT_X, y, "No books found.");  y += lh;
        g_rend->draw_text(TEXT_X, y, "Copy .epub files to /epub on SD card.");
        display.showPage(framebuf, true);
        return;
    }

    render_cover_thumbnail(g_sel_epub);

    // How many items fit between header and the two footer lines
    const int footer_lines = 2;
    const int avail = g_rend->get_page_height() - y - lh * footer_lines;
    const int max_vis = avail / lh;

    // Scroll to keep g_sel_epub visible
    int scroll = g_sel_epub - max_vis / 2;
    if (scroll < 0) scroll = 0;
    if (scroll > g_state.num_epubs - max_vis) scroll = max(0, g_state.num_epubs - max_vis);

    for (int i = scroll; i < g_state.num_epubs && y + lh <= g_rend->get_page_height() - lh * footer_lines; i++) {
        const EpubListItem &it = g_state.epub_list[i];
        bool sel = (i == g_sel_epub);

        // Truncate title to 32 chars; prepend cursor
        char title[33];
        strncpy(title, it.title, 32);
        title[32] = '\0';

        char line[40];
        snprintf(line, sizeof(line), "%s %s", sel ? ">" : " ", title);
        g_rend->draw_text(TEXT_X, y, line, sel);
        y += lh;
    }

    // Progress for currently selected book
    const EpubListItem &sel = g_state.epub_list[g_sel_epub];
    char prog[48] = "";
    if (sel.pages_in_current_section > 0)
        snprintf(prog, sizeof(prog), "  Section %d  Page %d/%d",
                 sel.current_section + 1,
                 sel.current_page    + 1,
                 sel.pages_in_current_section);
    else
        strncpy(prog, "  (not yet opened)", sizeof(prog) - 1);
    g_rend->draw_text(TEXT_X, g_rend->get_page_height() - lh * 2, prog);

    // Button hint
    g_rend->draw_text(TEXT_X, g_rend->get_page_height() - lh, "PREV/NEXT: navigate   SELECT: open");
    display.showPage(framebuf, true);
}

static void render_menu() {
    g_rend->clear_screen();
    const int lh = g_rend->get_line_height();
    int y = g_rend->get_page_height() / 2 - lh * 3;

    g_rend->draw_text(0, y, "MENU", true);
    y += lh + lh / 2;

    const char *opts[] = { "Continue Reading", "Go to Library", "Sleep" };
    for (int i = 0; i < 3; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", (i == g_menu_sel) ? ">" : " ", opts[i]);
        g_rend->draw_text(0, y, line, i == g_menu_sel);
        y += lh;
    }
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
        if (g_menu_sel < 2) { g_menu_sel++; render_menu(); }
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
        } else {
            // Sleep
            save_position();
            enter_deep_sleep();  // never returns
        }
    }
}

// =============================================================================
// Arduino entry points
// =============================================================================

void setup() {
    Serial.begin(115200);

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
        g_last_activity = millis();
        switch (g_app_state) {
            case ST_LIBRARY: handle_library(b); break;
            case ST_READER:  handle_reader(b);  break;
            case ST_MENU:    handle_menu(b);    break;
        }
    }

    // Idle timeout — save position then deep sleep
    if (millis() - g_last_activity > SLEEP_TIMEOUT_MS) {
        save_position();
        enter_deep_sleep();  // never returns
    }

    yield();
    delay(20);  // ~50 Hz button polling
}
