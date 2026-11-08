"""Endpoint tests for the library service.

Each test gets its own SQLite file so ids and counts are deterministic.
"""

import importlib
import tempfile
from pathlib import Path

import pytest
from fastapi.testclient import TestClient


@pytest.fixture()
def client(monkeypatch):
    with tempfile.TemporaryDirectory() as directory:
        monkeypatch.setenv("TOASTER_DATABASE",
                           str(Path(directory) / "test.sqlite"))
        import app as app_module
        import catalog as catalog_module

        # Importing the module opens a database immediately, so that first
        # connection is released before the reload replaces it -- otherwise it
        # leaks and keeps the temporary file open.
        if hasattr(app_module, "database"):
            app_module.database.close()

        # The module holds the database at import time, so it is reloaded to
        # pick up the temporary path.
        importlib.reload(app_module)
        catalog_module.clear_cache()
        try:
            yield TestClient(app_module.app)
        finally:
            # The temporary directory cannot be removed on Windows while
            # SQLite still holds the file open.
            app_module.database.close()


@pytest.fixture()
def games_folder():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "Zelda.nes").write_bytes(b"NES\x1a" + bytes(16))
        (root / "Tetris.gb").write_bytes(bytes(32))
        (root / "Zelda.png").write_bytes(b"\x89PNG")
        (root / "notes.txt").write_text("ignored")
        yield str(root)


def test_health(client):
    body = client.get("/health").json()
    assert body["status"] == "ok"
    assert "version" in body


def test_scan_finds_supported_files_only(client, games_folder):
    body = client.post("/scan", json={"folder": games_folder}).json()
    assert body["scanned"] == 2

    titles = {game["title"] for game in client.get("/games").json()}
    assert titles == {"Zelda", "Tetris"}


def test_scan_detects_systems(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    systems = {g["title"]: g["system"] for g in client.get("/games").json()}
    assert systems == {"Zelda": "NES", "Tetris": "Game Boy"}


def test_scan_attaches_sibling_artwork(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    zelda = next(g for g in client.get("/games").json() if g["title"] == "Zelda")
    assert zelda["artwork"].endswith("Zelda.png")


def test_scan_is_idempotent(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    client.post("/scan", json={"folder": games_folder})
    assert len(client.get("/games").json()) == 2


def test_scan_prunes_deleted_files(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    Path(games_folder, "Tetris.gb").unlink()

    body = client.post("/scan", json={"folder": games_folder}).json()
    assert body["removed"] == 1
    assert len(client.get("/games").json()) == 1


def test_scan_can_keep_missing_rows(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    Path(games_folder, "Tetris.gb").unlink()

    body = client.post(
        "/scan", json={"folder": games_folder, "prune_missing": False}
    ).json()
    assert body["removed"] == 0
    assert len(client.get("/games").json()) == 2


def test_scan_preserves_favorites(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]
    client.post(f"/games/{game_id}/favorite", json={"favorite": True})

    client.post("/scan", json={"folder": games_folder})
    assert len(client.get("/games?filter=favorites").json()) == 1


def test_missing_folder_is_a_client_error(client):
    response = client.post("/scan", json={"folder": "/definitely/not/here"})
    assert response.status_code == 400


def test_favorite_round_trip(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    assert client.post(
        f"/games/{game_id}/favorite", json={"favorite": True}
    ).status_code == 200
    assert len(client.get("/games?filter=favorites").json()) == 1
    assert len(client.get("/games/favorites").json()) == 1

    client.post(f"/games/{game_id}/favorite", json={"favorite": False})
    assert client.get("/games?filter=favorites").json() == []


def test_played_increments_count_and_lists_recent(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    games = client.get("/games").json()
    client.post(f"/games/{games[1]['id']}/played")

    recent = client.get("/games?filter=recent").json()
    assert len(recent) == 1
    assert recent[0]["play_count"] == 1
    assert recent[0]["last_played"] is not None

    client.post(f"/games/{games[1]['id']}/played")
    assert client.get("/games?filter=recent").json()[0]["play_count"] == 2


def test_recent_is_ordered_most_recent_first(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    games = client.get("/games").json()
    client.post(f"/games/{games[0]['id']}/played")
    client.post(f"/games/{games[1]['id']}/played")

    recent = client.get("/games?filter=recent").json()
    assert len(recent) == 2
    assert recent[0]["last_played"] >= recent[1]["last_played"]


def test_tags_are_normalised_and_deduplicated(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    body = client.put(
        f"/games/{game_id}/tags",
        json={"tags": ["RPG", " rpg ", "Action", ""]},
    ).json()
    assert body["tags"] == ["action", "rpg"]


def test_tags_can_be_cleared(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]
    client.put(f"/games/{game_id}/tags", json={"tags": ["rpg"]})

    body = client.put(f"/games/{game_id}/tags", json={"tags": []}).json()
    assert body["tags"] == []


def test_pagination(client, games_folder):
    client.post("/scan", json={"folder": games_folder})

    first = client.get("/games?limit=1").json()
    second = client.get("/games?limit=1&offset=1").json()
    assert len(first) == 1
    assert len(second) == 1
    assert first[0]["id"] != second[0]["id"]


def test_bad_filter_is_rejected(client):
    assert client.get("/games?filter=nonsense").status_code == 422


def test_unknown_game_is_404(client):
    assert client.get("/games/9999").status_code == 404
    assert client.post("/games/9999/played").status_code == 404
    assert client.post(
        "/games/9999/favorite", json={"favorite": True}
    ).status_code == 404
    assert client.put(
        "/games/9999/tags", json={"tags": []}
    ).status_code == 404


def test_game_lookup_by_id(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    listed = client.get("/games").json()[0]
    fetched = client.get(f"/games/{listed['id']}").json()
    assert fetched == listed


def test_statistics(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    stats = client.get("/statistics").json()

    assert stats["total"] == 2
    assert stats["favorites"] == 0
    assert stats["plays"] == 0
    assert {entry["system"] for entry in stats["systems"]} == {
        "NES", "Game Boy"
    }


def test_statistics_on_an_empty_library(client):
    stats = client.get("/statistics").json()
    assert stats["total"] == 0
    assert stats["systems"] == []


def test_catalog_requires_a_key(client, monkeypatch):
    monkeypatch.delenv("THEGAMESDB_API_KEY", raising=False)
    assert client.get("/catalog/search?q=mario").status_code == 503


def test_catalog_rejects_empty_queries(client, monkeypatch):
    monkeypatch.setenv("THEGAMESDB_API_KEY", "dummy")
    assert client.get("/catalog/search?q=%20").status_code == 400


def test_catalog_maps_upstream_results(client, monkeypatch):
    import catalog as catalog_module

    monkeypatch.setenv("THEGAMESDB_API_KEY", "dummy")
    catalog_module.clear_cache()

    payload = {
        "data": {"games": [
            {"id": 7, "game_title": "Zelda", "overview": "An adventure",
             "release_date": "1986-02-21", "platform": 7},
        ]},
        "include": {"boxart": {"7": {"thumb": ["https://example/zelda.png"]}}},
    }

    import json
    monkeypatch.setattr(catalog_module, "_fetch",
                        lambda query, api_key: json.dumps(payload))

    results = client.get("/catalog/search?q=zelda").json()
    assert len(results) == 1
    assert results[0]["title"] == "Zelda"
    assert results[0]["artwork"] == "https://example/zelda.png"


def test_catalog_tolerates_missing_boxart(client, monkeypatch):
    import catalog as catalog_module
    import json

    monkeypatch.setenv("THEGAMESDB_API_KEY", "dummy")
    catalog_module.clear_cache()

    payload = {"data": {"games": [{"id": 9, "game_title": "Unknown"}]},
               "include": {}}
    monkeypatch.setattr(catalog_module, "_fetch",
                        lambda query, api_key: json.dumps(payload))

    results = client.get("/catalog/search?q=unknown").json()
    assert results[0]["artwork"] is None
    assert results[0]["overview"] == ""


def test_saves_start_empty(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    assert client.get(f"/saves/{game_id}").json() == []


def test_a_save_round_trips(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    payload = b"battery contents"
    put = client.put(f"/saves/{game_id}/srm", content=payload)
    assert put.status_code == 200
    assert put.json()["size"] == len(payload)

    got = client.get(f"/saves/{game_id}/srm")
    assert got.status_code == 200
    assert got.content == payload
    # The digest the listing reports has to match the bytes served, or the
    # client's "identical, nothing to do" shortcut would be wrong.
    assert got.headers["X-Save-Digest"] == put.json()["digest"]


def test_a_second_upload_replaces_the_first(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    client.put(f"/saves/{game_id}/state3", content=b"first")
    client.put(f"/saves/{game_id}/state3", content=b"second")

    assert client.get(f"/saves/{game_id}/state3").content == b"second"
    listing = client.get(f"/saves/{game_id}").json()
    assert len(listing) == 1
    assert listing[0]["size"] == len(b"second")


def test_saves_are_listed_per_kind(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    client.put(f"/saves/{game_id}/srm", content=b"a")
    client.put(f"/saves/{game_id}/state0", content=b"bb")

    kinds = [entry["kind"] for entry in client.get(f"/saves/{game_id}").json()]
    assert kinds == ["srm", "state0"]


def test_an_unknown_save_kind_is_rejected(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    assert client.put(f"/saves/{game_id}/exe", content=b"x").status_code == 400
    assert client.get(f"/saves/{game_id}/exe").status_code == 400


def test_a_traversing_save_kind_cannot_escape_the_folder(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    # The whitelist has to reject this before it ever reaches a filename.
    assert client.put(
        f"/saves/{game_id}/..%2f..%2fescaped", content=b"x"
    ).status_code in (400, 404)


def test_an_empty_save_is_rejected(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    assert client.put(f"/saves/{game_id}/srm", content=b"").status_code == 400


def test_saves_for_an_unknown_game_are_404(client):
    assert client.get("/saves/999").status_code == 404
    assert client.put("/saves/999/srm", content=b"x").status_code == 404


def test_a_missing_save_is_404(client, games_folder):
    client.post("/scan", json={"folder": games_folder})
    game_id = client.get("/games").json()[0]["id"]

    assert client.get(f"/saves/{game_id}/state9").status_code == 404
