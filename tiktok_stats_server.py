"""
Apollova TikTok Stats Server

Flask server that scrapes public TikTok profile data, merges it with
manually-entered 7-day analytics, and exposes a JSON API for the
ESP32 dashboard to poll over WiFi.

Runs permanently on the Apollova Windows 11 laptop.
Start with: python tiktok_stats_server.py
"""

import json
import re
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any

import requests
from flask import Flask, Response, jsonify

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SERVER_VERSION = "1.0.0"
CACHE_TTL_SECONDS = 3600  # 1 hour between TikTok scrapes
SCRAPE_DELAY_SECONDS = 3  # Pause between per-account requests
STATS_FILE = Path(__file__).parent / "stats_manual.json"

ACCOUNTS = [
    {
        "handle": "apollovaaa",
        "display": "Aurora",
        "color": "#8B5CF6",
    },
    {
        "handle": "apollovaonyx",
        "display": "Onyx",
        "color": "#71717A",
    },
    {
        "handle": "apollovamono",
        "display": "Mono",
        "color": "#F59E0B",
    },
]

SCRAPE_HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/123.0.0.0 Safari/537.36"
    ),
    "Accept-Language": "en-US,en;q=0.9",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "Referer": "https://www.tiktok.com/",
}

# ---------------------------------------------------------------------------
# Global cache (thread-safe)
# ---------------------------------------------------------------------------

_cache: dict[str, Any] = {}
_cache_lock = threading.Lock()
_cache_updated_at: float = 0.0

# ---------------------------------------------------------------------------
# Logging helper
# ---------------------------------------------------------------------------


def log(msg: str) -> None:
    """Print a timestamped log message to stdout."""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


# ---------------------------------------------------------------------------
# Manual stats file I/O
# ---------------------------------------------------------------------------


def _default_manual_stats() -> dict[str, dict[str, Any]]:
    """Return the default structure for stats_manual.json."""
    return {
        acct["handle"]: {
            "views_7d": 0,
            "likes_7d": 0,
            "_note": "Update from TikTok Studio > Analytics > Last 7 days",
        }
        for acct in ACCOUNTS
    }


def load_manual_stats() -> dict[str, dict[str, Any]]:
    """Load 7-day manual stats from disk. Creates the file if missing."""
    if not STATS_FILE.exists():
        log(f"Creating {STATS_FILE.name} with default values")
        save_manual_stats(_default_manual_stats())

    try:
        raw = STATS_FILE.read_text(encoding="utf-8")
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise ValueError("Root is not a dict")
        return data
    except (json.JSONDecodeError, ValueError, OSError) as exc:
        log(f"WARNING: Could not read {STATS_FILE.name}: {exc} — using zeros")
        return _default_manual_stats()


def save_manual_stats(data: dict[str, dict[str, Any]]) -> None:
    """Write manual stats to disk."""
    try:
        STATS_FILE.write_text(
            json.dumps(data, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    except OSError as exc:
        log(f"ERROR: Could not write {STATS_FILE.name}: {exc}")


# ---------------------------------------------------------------------------
# TikTok public profile scraping
# ---------------------------------------------------------------------------


def scrape_account(handle: str) -> dict[str, int]:
    """Scrape public stats from a TikTok profile page.

    Returns a dict with keys: followerCount, heartCount, videoCount.
    Returns empty dict on any failure.
    """
    url = f"https://www.tiktok.com/@{handle}"
    try:
        resp = requests.get(url, headers=SCRAPE_HEADERS, timeout=15)
        resp.raise_for_status()
    except requests.HTTPError as exc:
        log(f"HTTP error scraping @{handle}: {exc}")
        return {}
    except requests.Timeout:
        log(f"Timeout scraping @{handle}")
        return {}
    except requests.ConnectionError as exc:
        log(f"Connection error scraping @{handle}: {exc}")
        return {}
    except requests.RequestException as exc:
        log(f"Request error scraping @{handle}: {exc}")
        return {}

    html = resp.text

    # Extract __UNIVERSAL_DATA_FOR_REHYDRATION__ JSON blob
    pattern = (
        r'<script\s+id="__UNIVERSAL_DATA_FOR_REHYDRATION__"[^>]*>'
        r'(.*?)</script>'
    )
    match = re.search(pattern, html, re.DOTALL)
    if not match:
        log(f"Could not find rehydration script for @{handle}")
        return {}

    try:
        data = json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        log(f"JSON parse error for @{handle}: {exc}")
        return {}

    # Navigate to stats
    try:
        stats = (
            data["__DEFAULT_SCOPE__"]["webapp.user-detail"]["userInfo"]["stats"]
        )
        return {
            "followerCount": int(stats.get("followerCount", 0)),
            "heartCount": int(stats.get("heartCount", 0)),
            "videoCount": int(stats.get("videoCount", 0)),
        }
    except (KeyError, TypeError) as exc:
        log(f"Could not extract stats for @{handle}: {exc}")
        return {}


def scrape_all_accounts() -> dict[str, dict[str, int]]:
    """Scrape all configured accounts with delay between each."""
    results: dict[str, dict[str, int]] = {}
    for i, acct in enumerate(ACCOUNTS):
        handle = acct["handle"]
        log(f"Scraping @{handle}...")
        results[handle] = scrape_account(handle)
        # Sleep between requests (but not after the last one)
        if i < len(ACCOUNTS) - 1:
            time.sleep(SCRAPE_DELAY_SECONDS)
    return results


# ---------------------------------------------------------------------------
# Cache management
# ---------------------------------------------------------------------------


def build_response() -> dict[str, Any]:
    """Build the full JSON response by merging scraped + manual data."""
    global _cache_updated_at

    scraped = scrape_all_accounts()
    manual = load_manual_stats()
    now = datetime.now()

    accounts_data = []
    for acct in ACCOUNTS:
        handle = acct["handle"]
        s = scraped.get(handle, {})
        m = manual.get(handle, {})

        accounts_data.append({
            "handle": handle,
            "display": acct["display"],
            "color": acct["color"],
            "followers": s.get("followerCount", 0),
            "total_likes": s.get("heartCount", 0),
            "video_count": s.get("videoCount", 0),
            "views_7d": int(m.get("views_7d", 0)),
            "likes_7d": int(m.get("likes_7d", 0)),
        })

        status = "OK" if s else "FAILED"
        log(
            f"  {acct['display']:8s} ({status}): "
            f"followers={s.get('followerCount', '?'):>8}  "
            f"views_7d={m.get('views_7d', 0):>10}  "
            f"likes_7d={m.get('likes_7d', 0):>8}"
        )

    response = {
        "accounts": accounts_data,
        "last_updated": now.strftime("%Y-%m-%dT%H:%M:%S"),
        "cache_age_seconds": 0,
        "server_version": SERVER_VERSION,
    }

    with _cache_lock:
        _cache.clear()
        _cache.update(response)
        _cache_updated_at = time.time()

    log("Cache updated successfully")
    return response


def refresh_loop() -> None:
    """Background thread: refresh cache every CACHE_TTL_SECONDS."""
    while True:
        time.sleep(CACHE_TTL_SECONDS)
        log("Background refresh starting...")
        try:
            build_response()
        except Exception as exc:
            log(f"ERROR in background refresh: {exc}")


# ---------------------------------------------------------------------------
# Flask app
# ---------------------------------------------------------------------------

app = Flask(__name__)


@app.route("/api/stats")
def api_stats() -> tuple[Response, int]:
    """Return full stats JSON for all accounts."""
    with _cache_lock:
        if not _cache:
            return jsonify({
                "error": "Stats not ready, retry in 15 seconds"
            }), 503

        response = dict(_cache)
        age = int(time.time() - _cache_updated_at)
        response["cache_age_seconds"] = age

    return jsonify(response), 200


@app.route("/api/health")
def api_health() -> tuple[Response, int]:
    """Return server health and cache status."""
    with _cache_lock:
        has_cache = bool(_cache)
        age = int(time.time() - _cache_updated_at) if has_cache else -1

    next_refresh = max(0, CACHE_TTL_SECONDS - age) if has_cache else 0

    return jsonify({
        "status": "ok" if has_cache else "warming_up",
        "cache_age_seconds": age,
        "next_refresh_seconds": next_refresh,
        "account_count": len(ACCOUNTS),
        "server_version": SERVER_VERSION,
    }), 200


@app.route("/")
def index() -> str:
    """Simple HTML status page."""
    with _cache_lock:
        has_cache = bool(_cache)
        age = int(time.time() - _cache_updated_at) if has_cache else -1

    status = "Ready" if has_cache else "Warming up..."
    age_str = f"{age}s ago" if age >= 0 else "never"

    return f"""<!DOCTYPE html>
<html>
<head><title>Apollova Stats Server</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 600px; margin: 40px auto;
         padding: 0 20px; background: #0d0d0d; color: #e0e0e0; }}
  h1 {{ color: #8B5CF6; }}
  a {{ color: #69C9D0; }}
  .status {{ padding: 12px; background: #1a1a1a; border-radius: 8px; margin: 16px 0; }}
</style>
</head>
<body>
  <h1>Apollova Stats Server</h1>
  <div class="status">
    <p><strong>Status:</strong> {status}</p>
    <p><strong>Last updated:</strong> {age_str}</p>
    <p><strong>Accounts:</strong> {len(ACCOUNTS)}</p>
    <p><strong>Version:</strong> {SERVER_VERSION}</p>
  </div>
  <p>API endpoints:</p>
  <ul>
    <li><a href="/api/stats">/api/stats</a> — Full stats JSON</li>
    <li><a href="/api/health">/api/health</a> — Server health</li>
  </ul>
</body>
</html>"""


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    log("=" * 50)
    log("Apollova TikTok Stats Server")
    log(f"Version {SERVER_VERSION}")
    log("=" * 50)

    # Ensure manual stats file exists
    load_manual_stats()

    # Initial fetch (blocking, ~15 seconds)
    log("Performing initial data fetch (this takes ~15 seconds)...")
    try:
        build_response()
    except Exception as exc:
        log(f"ERROR during initial fetch: {exc}")
        log("Server will start with empty cache — will retry in background")

    # Start background refresh thread
    refresh_thread = threading.Thread(target=refresh_loop, daemon=True)
    refresh_thread.start()
    log(f"Background refresh thread started (every {CACHE_TTL_SECONDS}s)")

    # Start Flask
    log("Starting Flask on http://0.0.0.0:5000")
    log("Press Ctrl+C to stop")
    log("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
