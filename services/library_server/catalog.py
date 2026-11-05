"""TheGamesDB catalog lookups.

Results are metadata only. Nothing here downloads a game; Toaster launches
only from the local paths a scan found.
"""

import json
import os
from functools import lru_cache
from urllib.error import URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


class CatalogUnavailable(RuntimeError):
    """Raised when the catalog cannot be reached or is not configured."""


@lru_cache(maxsize=512)
def _fetch(query: str, api_key: str) -> str:
    url = (
        "https://api.thegamesdb.net/v1/Games/ByGameName"
        f"?apikey={quote(api_key)}&name={quote(query)}&include=boxart"
    )
    request = Request(url, headers={"User-Agent": "Toaster/0.2"})
    with urlopen(request, timeout=10) as response:
        return response.read().decode("utf-8")


def clear_cache() -> None:
    _fetch.cache_clear()


def search_catalog(query: str) -> list[dict]:
    query = query.strip()
    if not query:
        raise ValueError("Search text is required")

    api_key = os.environ.get("THEGAMESDB_API_KEY")
    if not api_key:
        raise CatalogUnavailable(
            "Set THEGAMESDB_API_KEY to enable catalog search"
        )

    try:
        payload = _fetch(query, api_key)
        data = json.loads(payload)
    except (OSError, URLError, TimeoutError, json.JSONDecodeError) as error:
        raise CatalogUnavailable(str(error)) from error

    boxart = data.get("include", {}).get("boxart", {})

    def thumb(game_id) -> str | None:
        entry = boxart.get(str(game_id)) or {}
        images = entry.get("thumb") or [None]
        return images[0] if images else None

    # The previous version built this list twice and discarded the first copy.
    return [
        {
            "id": item.get("id"),
            "title": item.get("game_title", "") or "",
            "overview": item.get("overview", "") or "",
            "release_date": item.get("release_date", "") or "",
            "platform": item.get("platform", "") or "",
            "artwork": thumb(item.get("id")),
        }
        for item in data.get("data", {}).get("games", [])
    ]
