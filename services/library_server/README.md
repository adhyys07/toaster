# Toaster library service
Toaster library services fetch games lists and ratings from a public server called GamesDB so the player doesn't need to leave the emulator to choose the next game from. For developers please follow the below guide to get your API key and setup this service

Install dependencies and run from this directory:

```powershell
python -m pip install -r requirements.txt
python -m uvicorn app:app --host 127.0.0.1 --port 8765
```

The service listens on `http://127.0.0.1:8765`. Scan a folder from Toaster
with:

```powershell
build\emu.exe --games-dir D:\toaster\games `
  --library-server http://127.0.0.1:8765
```

The database stores local game paths, favorites, tags, artwork sidecars,
recent-play timestamps, and play counts. It does not store or distribute ROMs
or BIOS files.

Set `THEGAMESDB_API_KEY` to enable metadata catalog search:

```powershell
$env:THEGAMESDB_API_KEY = "your-key"
```

Catalog search is metadata-only. Toaster launches only games already present
in the local library; this service does not download ROMs.

The database defaults to `toaster-library.sqlite` in this directory. Set
`TOASTER_DATABASE` to put it somewhere persistent:

```powershell
$env:TOASTER_DATABASE = "D:\toaster\data\library.sqlite"
```

## Endpoints

| Endpoint | Purpose |
|---|---|
| `GET /health` | Liveness and version |
| `POST /scan` | Scan a local games folder; also prunes rows whose file is gone |
| `GET /games` | List locally available games; takes `limit` and `offset` |
| `GET /games?filter=favorites` | List favorite games |
| `GET /games?filter=recent` | List recently played games, newest first |
| `GET /games/{id}` | One game by id |
| `POST /games/{id}/favorite` | Change a favorite flag |
| `POST /games/{id}/played` | Record a launch and increment the play count |
| `PUT /games/{id}/tags` | Store user tags, normalised and deduplicated |
| `GET /statistics` | Library totals and per-system counts |
| `GET /catalog/search?q=...` | Search metadata without downloading games |

`POST /scan` accepts `{"folder": "...", "prune_missing": false}` to keep rows
for files that are no longer present.

## Tests

```powershell
python -m pip install -r requirements-dev.txt
python -m pytest .
```
