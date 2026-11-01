# Toaster library service

Install dependencies and run:

```powershell
python -m pip install -r requirements.txt
python -m uvicorn app:app --host 127.0.0.1 --port 8765
```

Set `THEGAMESDB_API_KEY` to enable metadata catalog search:

```powershell
$env:THEGAMESDB_API_KEY = "your-key"
```

Catalog search is metadata-only. Toaster launches only games already present
in the local library; this service does not download ROMs.
