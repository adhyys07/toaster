import hashlib
import os
import re
import time

from fastapi import FastAPI, HTTPException, Query, Request, Response
from pydantic import BaseModel, Field

from catalog import CatalogUnavailable, search_catalog
from database import GameDatabase
from scanner import scan_rom_folder

app = FastAPI(title="Toaster Library Service", version="0.2.0")

database = GameDatabase(
    os.environ.get("TOASTER_DATABASE", "toaster-library.sqlite")
)


class ScanRequest(BaseModel):
    folder: str
    # A rescan after deleting a ROM should drop the row, not keep a dead entry.
    prune_missing: bool = True


class FavoriteRequest(BaseModel):
    favorite: bool


class TagsRequest(BaseModel):
    tags: list[str] = Field(default_factory=list, max_length=64)


# Battery saves and the ten state slots, and nothing else. `kind` lands in
# a filename, so it is matched against this rather than sanitised: a
# whitelist cannot be talked out of rejecting "../../etc/passwd".
SAVE_KIND = re.compile(r"^(srm|sav|state[0-9])$")

# A save that will not fit in memory is not a save; refusing early keeps
# one bad client from exhausting the server.
MAX_SAVE_BYTES = 32 * 1024 * 1024


def _checked_kind(kind: str) -> str:
    if not SAVE_KIND.match(kind):
        raise HTTPException(status_code=400, detail="Unknown save kind")
    return kind


def _checked_game(game_id: int) -> dict:
    found = database.get_game(game_id)
    if not found:
        raise HTTPException(status_code=404, detail="Game not found")
    return found


@app.get("/health")
def health():
    return {"status": "ok", "version": app.version}


@app.post("/scan")
def scan(request: ScanRequest):
    try:
        scanned = database.upsert_games(scan_rom_folder(request.folder))
    except FileNotFoundError as error:
        raise HTTPException(status_code=400, detail=str(error)) from error

    removed = (
        database.remove_missing(request.folder)
        if request.prune_missing else 0
    )
    return {"scanned": scanned, "removed": removed}


@app.get("/games")
def games(
    filter: str | None = Query(default=None, pattern="^(favorites|recent)$"),
    limit: int | None = Query(default=None, ge=1, le=5000),
    offset: int = Query(default=0, ge=0),
):
    return database.list_games(filter, limit, offset)


@app.get("/statistics")
def statistics():
    return database.statistics()


# These two must stay declared before /games/{game_id}, or FastAPI tries to
# parse "favorites" as an integer id.
@app.get("/games/favorites")
def favorites():
    return database.list_games("favorites")


@app.get("/games/recent")
def recent():
    return database.list_games("recent")


@app.get("/catalog/search")
def catalog_search(q: str):
    try:
        return search_catalog(q)
    except ValueError as error:
        raise HTTPException(status_code=400, detail=str(error)) from error
    except CatalogUnavailable as error:
        # 503 when the key is missing, since the caller can fix that; the
        # upstream being unreachable is also not the caller's fault.
        raise HTTPException(status_code=503, detail=str(error)) from error


@app.get("/games/{game_id}")
def game(game_id: int):
    found = database.get_game(game_id)
    if found is None:
        raise HTTPException(status_code=404, detail="Game not found")
    return found


@app.post("/games/{game_id}/favorite")
def favorite(game_id: int, request: FavoriteRequest):
    if not database.set_favorite(game_id, request.favorite):
        raise HTTPException(status_code=404, detail="Game not found")
    return {"success": True, "favorite": request.favorite}


@app.post("/games/{game_id}/played")
def played(game_id: int):
    if not database.mark_played(game_id, int(time.time())):
        raise HTTPException(status_code=404, detail="Game not found")
    return {"success": True}


@app.put("/games/{game_id}/tags")
def tags(game_id: int, request: TagsRequest):
    if not database.set_tags(game_id, request.tags):
        raise HTTPException(status_code=404, detail="Game not found")

    stored = database.get_game(game_id)["tags"]
    return {
        "success": True,
        "tags": [tag for tag in stored.split(",") if tag],
    }

@app.get("/saves/{game_id}")
def list_saves(game_id: int):
    _checked_game(game_id)
    return database.list_saves(game_id)


@app.put("/saves/{game_id}/{kind}")
async def put_save(game_id: int, kind: str, request: Request):
    _checked_game(game_id)
    _checked_kind(kind)

    body = await request.body()
    if not body:
        raise HTTPException(status_code=400, detail="Empty save")
    if len(body) > MAX_SAVE_BYTES:
        raise HTTPException(status_code=413, detail="Save too large")

    target = database.save_blob_path(game_id, kind)
    # Written beside the target and renamed, so a dropped connection cannot
    # leave a truncated save where a good one was.
    staging = target.with_suffix(".part")
    staging.write_bytes(body)
    staging.replace(target)

    digest = hashlib.sha256(body).hexdigest()
    database.record_save(
        game_id, kind, digest, len(body), int(time.time())
    )
    return {
        "success": True,
        "kind": kind,
        "digest": digest,
        "size": len(body),
    }


@app.get("/saves/{game_id}/{kind}")
def get_save(game_id: int, kind: str):
    _checked_game(game_id)
    _checked_kind(kind)

    record = database.get_save(game_id, kind)
    path = database.save_blob_path(game_id, kind)
    if not record or not path.is_file():
        raise HTTPException(status_code=404, detail="No save of that kind")

    return Response(
        content=path.read_bytes(),
        media_type="application/octet-stream",
        headers={"X-Save-Digest": record["digest"]},
    )
