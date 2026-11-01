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

Set `TOASTER_DATABASE` to choose a persistent database location:

```powershell
$env:TOASTER_DATABASE = "D:\toaster\data\library.sqlite"
```
