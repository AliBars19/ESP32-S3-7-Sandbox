"""
Apollova — 7-Day Analytics Update CLI

Interactive CLI for entering 7-day views and likes from TikTok Studio.
Reads and writes stats_manual.json in the same directory.

Usage: python update_stats.py
"""

import json
import re
import sys
from pathlib import Path

STATS_FILE = Path(__file__).parent / "stats_manual.json"

ACCOUNTS = [
    {"handle": "apollovaaa", "display": "Aurora"},
    {"handle": "apollovaonyx", "display": "Onyx"},
    {"handle": "apollovamono", "display": "Mono"},
]


def format_number(n: int) -> str:
    """Format a number with commas for display."""
    return f"{n:,}"


def parse_number(raw: str) -> int | None:
    """Parse a human-entered number string.

    Handles formats like: 1.2M, 345K, 1,234,567, 12345.
    Returns None if the input is empty or unparseable.
    """
    raw = raw.strip()
    if not raw:
        return None

    # Strip commas
    raw = raw.replace(",", "")

    # Handle M suffix (e.g. 1.2M)
    match = re.match(r'^(\d+(?:\.\d+)?)\s*[Mm]$', raw)
    if match:
        return int(float(match.group(1)) * 1_000_000)

    # Handle K suffix (e.g. 345K)
    match = re.match(r'^(\d+(?:\.\d+)?)\s*[Kk]$', raw)
    if match:
        return int(float(match.group(1)) * 1_000)

    # Plain integer
    try:
        return int(float(raw))
    except ValueError:
        return None


def load_stats() -> dict:
    """Load stats_manual.json, creating it with defaults if missing."""
    defaults = {
        acct["handle"]: {
            "views_7d": 0,
            "likes_7d": 0,
            "_note": "Update from TikTok Studio > Analytics > Last 7 days",
        }
        for acct in ACCOUNTS
    }

    if not STATS_FILE.exists():
        return defaults

    try:
        data = json.loads(STATS_FILE.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            return defaults
        # Ensure all accounts exist
        for acct in ACCOUNTS:
            if acct["handle"] not in data:
                data[acct["handle"]] = defaults[acct["handle"]]
        return data
    except (json.JSONDecodeError, OSError):
        return defaults


def save_stats(data: dict) -> None:
    """Write stats to disk."""
    STATS_FILE.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    """Run the interactive stats update CLI."""
    print()
    print("=" * 60)
    print("  Apollova — 7-Day Analytics Update")
    print("  Source: TikTok Studio > Analytics > Last 7 days")
    print("=" * 60)
    print("  Tip: You can type numbers like  1.2M  or  345K")
    print()

    data = load_stats()

    for acct in ACCOUNTS:
        handle = acct["handle"]
        display = acct["display"]
        entry = data.get(handle, {"views_7d": 0, "likes_7d": 0})
        current_views = int(entry.get("views_7d", 0))
        current_likes = int(entry.get("likes_7d", 0))

        print(f"  -- @{handle} ({display}) " + "-" * (42 - len(handle) - len(display)))
        print(
            f"     Current:      {format_number(current_views)} views"
            f"       {format_number(current_likes)} likes"
        )

        # Views
        raw = input(f"     7-day views  (Enter to keep): ").strip()
        new_views = parse_number(raw)
        if new_views is None:
            new_views = current_views
            if raw:
                print(f"     Invalid input, keeping {format_number(current_views)}")
        else:
            pass

        # Likes
        raw = input(f"     7-day likes  (Enter to keep): ").strip()
        new_likes = parse_number(raw)
        if new_likes is None:
            new_likes = current_likes
            if raw:
                print(f"     Invalid input, keeping {format_number(current_likes)}")
        else:
            pass

        # Update
        data[handle] = {
            "views_7d": new_views,
            "likes_7d": new_likes,
            "_note": "Update from TikTok Studio > Analytics > Last 7 days",
        }

        print(
            f"     Saved:        {format_number(new_views)} views"
            f"       {format_number(new_likes)} likes"
        )
        print()

    save_stats(data)

    print("=" * 60)
    print(f"  Saved to {STATS_FILE.name}")
    print("  The dashboard will update within 30 minutes.")
    print("=" * 60)
    print()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n  Cancelled.")
        sys.exit(0)
