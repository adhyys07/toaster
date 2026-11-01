import json
import os
import time
from urllib.parse import quote
from urllib.error import URLError
from urllib.request import Request, urlopen
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from database import GameDatabase
from scanner import scan_rom_folder

app = FastAPI(title="Toaster Library Service")

database = GameDatabase("toaster-library.sqlite")

class ScanRequest(BaseModel):
    folder: str

class FavoriteRequest(BaseModel):
    favorite: bool

class TagsRequest(BaseModel):
    tags: list[str]


@app.get("/health")
def health():
    return {"status": "ok"}

@app.post("/scan")
def scan(request: ScanRequest):
    count = 0

    try:
        for game in scan_rom_folder(request.folder):
            database.upsert_game(game)
            count += 1
    except FileNotFoundError as error:
        raise HTTPException(status_code=400, detail=str(error))

    return {"scanned": count}

@app.get("/games")
def games(filter: str | None = None):
    if filter == "favorites":
        return [
            game for game in database.list_games()
            if game["favorite"]
        ]
    if filter == "recent":
        return sorted(
            [game for game in database.list_games()
             if game["last_played"] is not None],
            key=lambda game: game["last_played"],
            reverse=True,
        )
    return database.list_games()

@app.get("/statistics")
def statistics():
    return database.statistics()

@app.get("/catalog/search")
def catalog_search(q: str):
    if not q.strip():
        raise HTTPException(status_code=400, detail="Search text is required")
    api_key = os.environ.get("THEGAMESDB_API_KEY")
    if not api_key:
        raise HTTPException(
            status_code=503,
            detail="Set THEGAMESDB_API_KEY to enable catalog search",
        )
    url = (
        "https://api.thegamesdb.net/v1/Games/ByGameName"
        f"?apikey={quote(api_key)}&name={quote(q)}&include=boxart"
    )
    try:
        request = Request(url, headers={"User-Agent": "Toaster/0.1"})
        with urlopen(request, timeout=10) as response:
            payload = response.read().decode("utf-8")
        data = json.loads(payload)
        return [
            {
                "id": item.get("id"),
                "title": item.get("game_title", ""),
                "overview": item.get("overview", ""),
                "release_date": item.get("release_date", ""),
                "platform": item.get("platform", ""),
                "artwork": item.get("boxart", {}).get("thumb"),
            }
            for item in data.get("data", {}).get("games", [])
        ]
    except (OSError, URLError, TimeoutError, json.JSONDecodeError) as error:
        raise HTTPException(status_code=502, detail=str(error))


@app.get("/games/{game_id}")
def game(game_id: int):
    for item in database.list_games():
        if item["id"] == game_id:
            return item

    raise HTTPException(status_code=404, detail="Game not found")

@app.post("/games/{game_id}/favorite")
def favorite(game_id: int, request: FavoriteRequest):
    database.set_favorite(game_id, request.favorite)
    return {"success": True}


@app.post("/games/{game_id}/played")
def played(game_id: int):
    database.mark_played(game_id, int(time.time()))
    return {"success": True}

@app.put("/games/{game_id}/tags")
def tags(game_id: int, request: TagsRequest):
    if database.get_game(game_id) is None:
        raise HTTPException(status_code=404, detail="Game not found")
    database.set_tags(game_id, request.tags)
    return {"success": True, "tags": database.get_game(game_id)["tags"].split(",")}

@app.get("/games/favorites")
def favorites():
    return [
        game for game in database.list_games()
        if game["favorite"]
    ]


@app.get("/games/recent")
def recent():
    games = [
        game for game in database.list_games()
        if game["last_played"] is not None
    ]
    return sorted(
        games,
        key=lambda game: game["last_played"],
        reverse=True,
    )