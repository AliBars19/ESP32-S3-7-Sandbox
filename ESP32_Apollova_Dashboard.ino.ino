/**
 * Apollova TikTok Stats Dashboard
 * ESP32-S3-Touch-LCD-7 (Waveshare) — 800x480 IPS Display
 *
 * Polls a local Flask server for TikTok analytics and renders a
 * branded 3-account dashboard using LVGL 8.x.
 *
 * REQUIRED ARDUINO IDE SETTINGS:
 *   Board:            ESP32S3 Dev Module
 *   PSRAM:            OPI PSRAM
 *   Flash Size:       16MB
 *   Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
 *
 * REQUIRED LIBRARIES:
 *   Waveshare_ST7262_LVGL
 *   LVGL 8.3.11
 *   ArduinoJson 6.x (NOT v7)
 *   WiFi (built-in)
 *   HTTPClient (built-in)
 */

#include <Arduino.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// =========================================================================
// CONFIGURE THESE — WiFi and server settings
// =========================================================================
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"
#define SERVER_IP       "192.168.1.100"
#define SERVER_PORT     5000
#define TZ_OFFSET_SEC   3600    // 3600 = London BST, 0 = GMT winter

// =========================================================================
// Timing constants
// =========================================================================
#define FETCH_INTERVAL_MS   (30UL * 60UL * 1000UL)   // 30 minutes
#define FETCH_RETRY_MS      (60UL * 1000UL)           // 60 seconds on failure
#define CLOCK_UPDATE_MS     (30UL * 1000UL)           // 30 seconds
#define FETCH_INITIAL_DELAY 4000                       // 4s before first fetch
#define WIFI_RETRY_COUNT    40
#define WIFI_RETRY_DELAY_MS 500
#define HTTP_TIMEOUT_MS     15000

// =========================================================================
// Colour palette
// =========================================================================
#define C_BG        0x0D0D0D
#define C_HEADER    0x141414
#define C_CARD      0x1A1A1A
#define C_BORDER    0x2A2A2A
#define C_WHITE     0xFFFFFF
#define C_GRAY      0x9CA3AF
#define C_DIM       0x4B5563
#define C_GREEN     0x22C55E
#define C_RED       0xEF4444
#define C_AURORA    0x8B5CF6
#define C_ONYX      0x9CA3AF
#define C_MONO      0xF59E0B

// =========================================================================
// Layout constants
// =========================================================================
#define SCREEN_W        800
#define SCREEN_H        480
#define HEADER_H        58
#define FOOTER_H        26
#define CARD_MARGIN     11
#define CARD_W          252
#define CARD_H          350
#define CARD_Y          66
#define FOOTER_Y        (SCREEN_H - FOOTER_H)
#define CARD_INNER_PAD  16
#define ACCENT_H        5
#define CARD_RADIUS     10
#define NUM_ACCOUNTS    3

// =========================================================================
// Account data structures
// =========================================================================

struct AccountData {
    char handle[32];
    char display[32];
    long followers;
    long total_likes;
    long video_count;
    long views_7d;
    long likes_7d;
};

static AccountData g_accounts[NUM_ACCOUNTS] = {
    {"apollovaaa",   "Aurora", 0, 0, 0, 0, 0},
    {"apollovaonyx", "Onyx",   0, 0, 0, 0, 0},
    {"apollovamono", "Mono",   0, 0, 0, 0, 0},
};

// Account brand colours (indexed same as g_accounts)
static const uint32_t g_accent_colors[NUM_ACCOUNTS] = {
    C_AURORA, C_ONYX, C_MONO
};

// =========================================================================
// LVGL widget handles
// =========================================================================

struct CardWidgets {
    lv_obj_t *card;
    lv_obj_t *accent_stripe;
    lv_obj_t *lbl_name;
    lv_obj_t *lbl_handle;
    lv_obj_t *lbl_followers_title;
    lv_obj_t *lbl_followers_value;
    lv_obj_t *lbl_views_title;
    lv_obj_t *lbl_views_value;
    lv_obj_t *lbl_likes_title;
    lv_obj_t *lbl_likes_value;
    lv_obj_t *lbl_videos_title;
    lv_obj_t *lbl_videos_value;
};

static CardWidgets g_cards[NUM_ACCOUNTS];
static lv_obj_t *g_lbl_title;
static lv_obj_t *g_lbl_subtitle;
static lv_obj_t *g_lbl_time;
static lv_obj_t *g_lbl_footer;
static lv_obj_t *g_wifi_dot;

// =========================================================================
// State
// =========================================================================
static bool g_wifi_connected = false;
static bool g_data_loaded = false;
static unsigned long g_last_fetch_ms = 0;
static bool g_last_fetch_ok = false;

// =========================================================================
// Number formatting
// =========================================================================

/**
 * Format a number for display on the dashboard.
 *   n <= 0      -> "--"
 *   n < 1000    -> "987"
 *   n < 10000   -> "1,234"
 *   n < 1000000 -> "45.6K"
 *   n >= 1M     -> "1.2M"
 */
static void fmtNumber(long n, char *buf, size_t sz) {
    if (n <= 0) {
        strlcpy(buf, "--", sz);
    } else if (n < 1000) {
        snprintf(buf, sz, "%ld", n);
    } else if (n < 10000) {
        snprintf(buf, sz, "%ld,%03ld", n / 1000, n % 1000);
    } else if (n < 1000000) {
        snprintf(buf, sz, "%.1fK", n / 1000.0);
    } else {
        snprintf(buf, sz, "%.1fM", n / 1000000.0);
    }
}

// =========================================================================
// WiFi connection
// =========================================================================

static void wifiConnect() {
    Serial.printf("WiFi connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    for (int i = 0; i < WIFI_RETRY_COUNT; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            g_wifi_connected = true;
            Serial.println();
            Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());

            // Start NTP time sync (non-blocking)
            configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org");
            return;
        }
        Serial.print(".");
        delay(WIFI_RETRY_DELAY_MS);
    }

    Serial.println();
    Serial.println("WiFi connection FAILED — continuing without network");
    g_wifi_connected = false;
}

// =========================================================================
// LVGL helpers
// =========================================================================

/** Create a horizontal divider line inside a parent at given y offset. */
static lv_obj_t *createDivider(lv_obj_t *parent, int y, int width) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_pos(line, CARD_INNER_PAD, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

/** Create a label at absolute position within parent. */
static lv_obj_t *createLabel(lv_obj_t *parent, int x, int y,
                              const lv_font_t *font, uint32_t color,
                              const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

// =========================================================================
// Build dashboard UI
// =========================================================================

static void buildDashboard() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // --- Header bar ---
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, SCREEN_W, HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // APOLLOVA title (left side)
    g_lbl_title = createLabel(header, 20, 16, &lv_font_montserrat_22,
                               C_WHITE, "APOLLOVA");
    lv_obj_set_style_text_letter_space(g_lbl_title, 4, 0);

    // Subtitle (center area)
    g_lbl_subtitle = createLabel(header, 220, 22, &lv_font_montserrat_12,
                                  C_DIM, "TIKTOK ANALYTICS  \xC2\xB7  PAST 7 DAYS");

    // WiFi dot (right side)
    g_wifi_dot = lv_obj_create(header);
    lv_obj_set_size(g_wifi_dot, 8, 8);
    lv_obj_set_pos(g_wifi_dot, SCREEN_W - 80, 25);
    lv_obj_set_style_radius(g_wifi_dot, 4, 0);
    lv_obj_set_style_bg_color(g_wifi_dot,
        lv_color_hex(g_wifi_connected ? C_GREEN : C_RED), 0);
    lv_obj_set_style_bg_opa(g_wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_wifi_dot, 0, 0);
    lv_obj_clear_flag(g_wifi_dot, LV_OBJ_FLAG_SCROLLABLE);

    // Clock (rightmost)
    g_lbl_time = createLabel(header, SCREEN_W - 60, 20, &lv_font_montserrat_16,
                              C_GRAY, "--:--");

    // --- Three account cards ---
    int card_x_positions[NUM_ACCOUNTS] = {
        CARD_MARGIN,
        CARD_MARGIN + CARD_W + CARD_MARGIN,
        CARD_MARGIN + (CARD_W + CARD_MARGIN) * 2
    };

    for (int i = 0; i < NUM_ACCOUNTS; i++) {
        int cx = card_x_positions[i];
        uint32_t accent = g_accent_colors[i];
        int divider_w = CARD_W - (CARD_INNER_PAD * 2);

        // Card container
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, cx, CARD_Y);
        lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, CARD_RADIUS, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        g_cards[i].card = card;

        // Accent stripe (top coloured bar)
        g_cards[i].accent_stripe = lv_obj_create(card);
        lv_obj_set_size(g_cards[i].accent_stripe, CARD_W, ACCENT_H);
        lv_obj_set_pos(g_cards[i].accent_stripe, 0, 0);
        lv_obj_set_style_bg_color(g_cards[i].accent_stripe,
                                   lv_color_hex(accent), 0);
        lv_obj_set_style_bg_opa(g_cards[i].accent_stripe, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_cards[i].accent_stripe, 0, 0);
        lv_obj_set_style_radius(g_cards[i].accent_stripe, 0, 0);
        lv_obj_clear_flag(g_cards[i].accent_stripe, LV_OBJ_FLAG_SCROLLABLE);

        // Account display name
        g_cards[i].lbl_name = createLabel(card, CARD_INNER_PAD, 16,
            &lv_font_montserrat_22, C_WHITE, g_accounts[i].display);

        // Handle (e.g. @apollovaaa)
        char handle_buf[40];
        snprintf(handle_buf, sizeof(handle_buf), "@%s", g_accounts[i].handle);
        g_cards[i].lbl_handle = createLabel(card, CARD_INNER_PAD, 46,
            &lv_font_montserrat_14, accent, handle_buf);

        // --- Divider 1 ---
        createDivider(card, 70, divider_w);

        // FOLLOWERS section
        g_cards[i].lbl_followers_title = createLabel(card, CARD_INNER_PAD, 82,
            &lv_font_montserrat_12, C_DIM, "FOLLOWERS");
        g_cards[i].lbl_followers_value = createLabel(card, CARD_INNER_PAD, 98,
            &lv_font_montserrat_22, C_WHITE, "--");

        // --- Divider 2 ---
        createDivider(card, 132, divider_w);

        // 7-DAY VIEWS section
        g_cards[i].lbl_views_title = createLabel(card, CARD_INNER_PAD, 144,
            &lv_font_montserrat_12, C_DIM, "7-DAY VIEWS");
        g_cards[i].lbl_views_value = createLabel(card, CARD_INNER_PAD, 160,
            &lv_font_montserrat_22, accent, "--");

        // --- Divider 3 ---
        createDivider(card, 194, divider_w);

        // 7-DAY LIKES section
        g_cards[i].lbl_likes_title = createLabel(card, CARD_INNER_PAD, 206,
            &lv_font_montserrat_12, C_DIM, "7-DAY LIKES");
        g_cards[i].lbl_likes_value = createLabel(card, CARD_INNER_PAD, 222,
            &lv_font_montserrat_22, accent, "--");

        // --- Divider 4 ---
        createDivider(card, 256, divider_w);

        // VIDEOS section
        g_cards[i].lbl_videos_title = createLabel(card, CARD_INNER_PAD, 268,
            &lv_font_montserrat_12, C_DIM, "VIDEOS");
        g_cards[i].lbl_videos_value = createLabel(card, CARD_INNER_PAD, 284,
            &lv_font_montserrat_20, C_GRAY, "--");
    }

    // --- Footer bar ---
    lv_obj_t *footer = lv_obj_create(scr);
    lv_obj_set_size(footer, SCREEN_W, FOOTER_H);
    lv_obj_set_pos(footer, 0, FOOTER_Y);
    lv_obj_set_style_bg_color(footer, lv_color_hex(C_HEADER), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    g_lbl_footer = lv_label_create(footer);
    lv_label_set_text(g_lbl_footer, "Connecting to stats server...");
    lv_obj_set_style_text_color(g_lbl_footer, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_text_font(g_lbl_footer, &lv_font_montserrat_12, 0);
    lv_obj_center(g_lbl_footer);
}

// =========================================================================
// Update dashboard with current data
// =========================================================================

static void updateDashboard() {
    char buf[32];

    lvgl_port_lock(-1);

    for (int i = 0; i < NUM_ACCOUNTS; i++) {
        // Followers
        fmtNumber(g_accounts[i].followers, buf, sizeof(buf));
        lv_label_set_text(g_cards[i].lbl_followers_value, buf);

        // 7-day views
        fmtNumber(g_accounts[i].views_7d, buf, sizeof(buf));
        lv_label_set_text(g_cards[i].lbl_views_value, buf);

        // 7-day likes
        fmtNumber(g_accounts[i].likes_7d, buf, sizeof(buf));
        lv_label_set_text(g_cards[i].lbl_likes_value, buf);

        // Video count
        fmtNumber(g_accounts[i].video_count, buf, sizeof(buf));
        lv_label_set_text(g_cards[i].lbl_videos_value, buf);
    }

    // Update footer
    if (g_data_loaded) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            char footer_buf[80];
            snprintf(footer_buf, sizeof(footer_buf),
                     "Last updated %02d:%02d  \xC2\xB7  refreshes every 30 min",
                     timeinfo.tm_hour, timeinfo.tm_min);
            lv_label_set_text(g_lbl_footer, footer_buf);
        } else {
            lv_label_set_text(g_lbl_footer,
                "Data loaded  \xC2\xB7  refreshes every 30 min");
        }
    }

    // Update WiFi dot colour
    lv_obj_set_style_bg_color(g_wifi_dot,
        lv_color_hex(g_wifi_connected ? C_GREEN : C_RED), 0);

    lvgl_port_unlock();
}

// =========================================================================
// HTTP fetch and JSON parse
// =========================================================================

static bool fetchStats() {
    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_connected = false;
        Serial.println("WiFi disconnected — attempting reconnect");
        WiFi.reconnect();
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Reconnect failed");
            return false;
        }
        g_wifi_connected = true;
        Serial.printf("Reconnected: %s\n", WiFi.localIP().toString().c_str());
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/stats", SERVER_IP, SERVER_PORT);
    Serial.printf("Fetch: %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    // Parse JSON into heap-allocated document
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray accounts = doc["accounts"];
    if (accounts.isNull()) {
        Serial.println("No 'accounts' array in response");
        return false;
    }

    // Map JSON entries to g_accounts by array index (server sends in order)
    int count = 0;
    for (JsonObject acct : accounts) {
        if (count >= NUM_ACCOUNTS) break;

        const char *handle = acct["handle"] | "";
        const char *display = acct["display"] | "";

        strlcpy(g_accounts[count].handle, handle,
                sizeof(g_accounts[count].handle));
        strlcpy(g_accounts[count].display, display,
                sizeof(g_accounts[count].display));

        g_accounts[count].followers   = acct["followers"]   | 0L;
        g_accounts[count].total_likes = acct["total_likes"] | 0L;
        g_accounts[count].video_count = acct["video_count"] | 0L;
        g_accounts[count].views_7d    = acct["views_7d"]    | 0L;
        g_accounts[count].likes_7d    = acct["likes_7d"]    | 0L;

        char nbuf[16];
        fmtNumber(g_accounts[count].followers, nbuf, sizeof(nbuf));
        Serial.printf("  %-8s followers=%-8s", display, nbuf);
        fmtNumber(g_accounts[count].views_7d, nbuf, sizeof(nbuf));
        Serial.printf("  views_7d=%-8s", nbuf);
        fmtNumber(g_accounts[count].likes_7d, nbuf, sizeof(nbuf));
        Serial.printf("  likes_7d=%-8s\n", nbuf);

        count++;
    }

    Serial.printf("Parsed %d accounts\n", count);
    return count > 0;
}

// =========================================================================
// FreeRTOS tasks
// =========================================================================

/**
 * taskFetch — Runs on Core 0.
 * Fetches data from the Flask server and updates the dashboard.
 */
static void taskFetch(void *param) {
    (void)param;

    // Wait for display to be ready
    vTaskDelay(pdMS_TO_TICKS(FETCH_INITIAL_DELAY));

    for (;;) {
        Serial.println("--- Fetch cycle ---");

        bool ok = fetchStats();
        g_last_fetch_ok = ok;

        if (ok) {
            g_data_loaded = true;
            g_last_fetch_ms = millis();
            updateDashboard();
            Serial.println("Dashboard updated");
            vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
        } else {
            Serial.printf("Fetch failed — retrying in %lus\n",
                          FETCH_RETRY_MS / 1000);
            // Update WiFi dot even on failure
            lvgl_port_lock(-1);
            lv_obj_set_style_bg_color(g_wifi_dot,
                lv_color_hex(g_wifi_connected ? C_GREEN : C_RED), 0);
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(FETCH_RETRY_MS));
        }
    }
}

/**
 * taskClock — Runs on Core 1 (same as LVGL).
 * Updates the clock display every 30 seconds.
 */
static void taskClock(void *param) {
    (void)param;

    for (;;) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0)) {
            char timebuf[8];
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d",
                     timeinfo.tm_hour, timeinfo.tm_min);

            lvgl_port_lock(-1);
            lv_label_set_text(g_lbl_time, timebuf);
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(CLOCK_UPDATE_MS));
    }
}

// =========================================================================
// Arduino setup and loop
// =========================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("  Apollova Stats Dashboard    ");
    Serial.println("==============================");

    // 1. WiFi
    wifiConnect();

    // 2. Display + LVGL init
    Serial.println("Initialising display...");
    lcd_init();
    Serial.println("Display OK");

    // 3. Build UI
    lvgl_port_lock(-1);
    buildDashboard();
    lvgl_port_unlock();
    Serial.println("Dashboard built — showing loading state");

    // 4. Launch FreeRTOS tasks
    xTaskCreatePinnedToCore(taskFetch, "Fetch", 16384, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(taskClock, "Clock",  4096, NULL, 1, NULL, 1);
    Serial.println("Tasks started");
}

void loop() {
    // LVGL is handled by the Waveshare library's internal task
    delay(10);
}
