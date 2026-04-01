# Apollova TikTok Stats Dashboard

A physical desk dashboard that displays TikTok analytics for three Apollova
accounts on a Waveshare ESP32-S3-Touch-LCD-7 (800x480 IPS display).

```
                     TikTok.com
                         |
                    (scrape profiles)
                         |
    +--------------------v---------------------+
    |  Python Flask Server (Windows laptop)     |
    |  tiktok_stats_server.py  :5000            |
    |  + stats_manual.json (7-day analytics)    |
    +--------------------+---------------------+
                         |
                    (WiFi / HTTP JSON)
                         |
    +--------------------v---------------------+
    |  ESP32-S3-Touch-LCD-7                     |
    |  7" IPS display, 800x480                  |
    |  3 branded account cards                  |
    +------------------------------------------+
```

Accounts tracked:

| Account        | Template | Brand Colour        |
|----------------|----------|---------------------|
| @apollovaaa    | Aurora   | Purple (#8B5CF6)    |
| @apollovaonyx  | Onyx     | Silver (#71717A)    |
| @apollovamono  | Mono     | Amber (#F59E0B)     |

Each card displays: followers, 7-day views, 7-day likes, and video count.

---

## Prerequisites

### Python (server side)
- Python 3.10 or newer
- pip packages: `flask>=3.0.0`, `requests>=2.31.0`

### Arduino IDE (ESP32 side)
- Arduino IDE 2.x
- ESP32 Arduino core 2.x (Board Manager: "esp32 by Espressif Systems")
- Libraries (install from Library Manager):
  - **Waveshare_ST7262_LVGL** (search exact name)
  - **lvgl** version **8.3.11** (pin this version — do NOT use 9.x)
  - **ArduinoJson** version **6.x** by Benoit Blanchon (do NOT use 7.x)
  - WiFi and HTTPClient are built into the ESP32 core

---

## Server Setup

### 1. Install dependencies

```bash
cd apollova-dashboard
pip install -r requirements.txt
```

### 2. Find your PC's local IP address

On Windows, open a terminal and run:
```
ipconfig
```
Look for your WiFi adapter's **IPv4 Address** — typically `192.168.1.x` or
`192.168.0.x`. You will need this for the ESP32 sketch.

### 3. Start the server

```bash
python tiktok_stats_server.py
```

The server performs an initial TikTok scrape on startup (takes ~15 seconds),
then serves data at `http://0.0.0.0:5000`.

### 4. Verify it works

Open a browser and visit: `http://localhost:5000`

You should see a status page with links to the API endpoints. Click
`/api/stats` to see the JSON response.

---

## First Run

On first run, the server creates `stats_manual.json` with zeros for all
7-day analytics. The scraped data (followers, total likes, video count)
will populate immediately.

---

## Weekly Update Process

TikTok's 7-day view and like counts require Business API access, so they
must be entered manually from TikTok Studio.

### Steps:

1. Open TikTok Studio for each account
2. Go to **Analytics** and select **Last 7 days**
3. Note down the **Video Views** and **Likes** totals
4. Run the update script:

```bash
python update_stats.py
```

5. Enter the numbers when prompted (supports formats like `1.2M`, `345K`,
   `1,234,567`, or just `12345`)
6. Press Enter without typing to keep the current value

The dashboard will pick up the new values within 30 minutes (next fetch cycle).

---

## ESP32 Flash Procedure

### 1. Configure the sketch

Open `ESP32_Apollova_Dashboard/ESP32_Apollova_Dashboard.ino` and edit the
configuration section at the top:

```cpp
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"
#define SERVER_IP       "192.168.1.100"    // Your PC's local IP
#define SERVER_PORT     5000
#define TZ_OFFSET_SEC   3600               // 3600=BST, 0=GMT
```

### 2. Arduino IDE board settings

- **Board:** ESP32S3 Dev Module
- **PSRAM:** OPI PSRAM (CRITICAL — display won't work without this)
- **Flash Size:** 16MB
- **Partition Scheme:** Huge APP (3MB No OTA / 1MB SPIFFS)
- **Upload Speed:** 921600

### 3. Install libraries

In Arduino IDE: Sketch > Include Library > Manage Libraries:
- Search "Waveshare_ST7262_LVGL" and install
- Search "lvgl", find version 8.3.11, install
- Search "ArduinoJson" by Benoit Blanchon, install version 6.x

### 4. Flash

1. Connect the ESP32-S3 via USB
2. Select the correct port in Tools > Port
3. Click Upload
4. Open Serial Monitor at 115200 baud to see boot logs

---

## Windows Autostart

To have the server start automatically with Windows:

### Option A: Task Scheduler
1. Open Task Scheduler
2. Create a new task: "Apollova Stats Server"
3. Trigger: At log on
4. Action: Start a program
   - Program: `python` (or full path to python.exe)
   - Arguments: `tiktok_stats_server.py`
   - Start in: `C:\path\to\apollova-dashboard`
5. Settings: Allow task to run indefinitely

### Option B: Startup folder
1. Press Win+R, type `shell:startup`, press Enter
2. Create a file `start_apollova_server.bat`:
```batch
@echo off
cd /d "C:\path\to\apollova-dashboard"
python tiktok_stats_server.py
```

### Option C: Add to existing Apollova startup
If the Apollova pipeline already has a startup script or system tray app,
add the server launch there.

---

## Troubleshooting

### Server not reachable from ESP32
- Check Windows Firewall: temporarily disable it to test, then add an
  inbound rule for port 5000 if needed
- Ensure both devices are on the same WiFi network (2.4GHz — ESP32 does
  not support 5GHz)
- Verify the IP address matches: run `ipconfig` on the PC

### Display shows only white screen
- PSRAM must be set to "OPI PSRAM" in Arduino IDE board settings
- This is the most common issue — without PSRAM, the display cannot
  allocate framebuffers

### WiFi connects but fetch fails
- Check that `SERVER_IP` in the sketch matches your PC's actual IP
- Make sure the Flask server is running
- Check Serial Monitor for HTTP error codes

### TikTok returns 0 followers
- TikTok may have changed the `__UNIVERSAL_DATA_FOR_REHYDRATION__`
  script structure
- Check the server logs (stdout) for scraping errors
- The server will continue serving the last successful data

### LVGL font compile error
- Ensure the Waveshare library's `lv_conf.h` has the needed font sizes
  enabled (12, 14, 16, 18, 20, 22, 24, 28 are used)

### Dashboard shows "--" for all values
- This is normal on first boot before data is fetched
- Check Serial Monitor — it should show fetch attempts
- If WiFi dot is red, fix the WiFi connection first

### Server crashes on startup
- Make sure `flask` and `requests` are installed: `pip install -r requirements.txt`
- Check Python version is 3.10 or newer: `python --version`
