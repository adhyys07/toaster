import sqlite3
import threading
from pathlib import Path


class GameDatabase:
    # Every query returns the same column set, so the frontend can rely on it.
    _COLUMNS = """
        id, path, title, system, extension, artwork,
        favorite, last_played, play_count, tags
    """

    def __init__(self, database_path: str):
        self.database_path = Path(database_path)
        self.lock = threading.RLock()
        self.connection = sqlite3.connect(
            self.database_path,
            check_same_thread=False,
        )
        self.connection.row_factory = sqlite3.Row
        self.create_tables()

    def close(self):
        """Releases the database file.

        Windows refuses to delete a file that is still open, so tests and
        anything else with a short-lived database need this.
        """
        with self.lock:
            self.connection.close()

    def create_tables(self):
        self.connection.execute("""
            CREATE TABLE IF NOT EXISTS games (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL,
                system TEXT NOT NULL,
                extension TEXT NOT NULL,
                artwork TEXT,
                favorite INTEGER NOT NULL DEFAULT 0,
                last_played INTEGER
            )
        """)
        self.connection.commit()

        columns = {
            row["name"] for row in self.connection.execute(
                "PRAGMA table_info(games)"
            )
        }
        if "play_count" not in columns:
            self.connection.execute(
                "ALTER TABLE games ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0"
            )
        if "tags" not in columns:
            self.connection.execute(
                "ALTER TABLE games ADD COLUMN tags TEXT NOT NULL DEFAULT ''"
            )

        # The library window re-queries on every keystroke. Filtering in
        # Python meant reading the whole table each time; these let SQLite do
        # the work instead.
        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS games_favorite "
            "ON games(favorite) WHERE favorite = 1"
        )
        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS games_last_played "
            "ON games(last_played DESC)"
        )
        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS games_system ON games(system)"
        )

        # Save blobs live on disk; this table holds only what is needed
        # to decide whether a transfer is worth making.
        self.connection.execute("""
            CREATE TABLE IF NOT EXISTS saves (
                game_id INTEGER NOT NULL,
                kind TEXT NOT NULL,
                digest TEXT NOT NULL,
                size INTEGER NOT NULL,
                modified INTEGER NOT NULL,
                PRIMARY KEY (game_id, kind)
            )
        """)
        self.connection.commit()

        # WAL lets a read run while a scan is writing, which is what the
        # frontend does when it rescans in the background.
        self.connection.execute("PRAGMA journal_mode=WAL")
        self.connection.commit()

    def upsert_game(self, game: dict):
        self.upsert_games([game])

    def upsert_games(self, games) -> int:
        """Inserts a whole scan in one transaction.

        The previous version committed once per row, which made scanning a
        large folder far slower than it needed to be.
        """
        rows = [
            (
                game["path"], game["title"], game["system"],
                game["extension"], game.get("artwork"),
                int(game.get("favorite", False)), game.get("last_played"),
            )
            for game in games
        ]
        if not rows:
            return 0

        with self.lock, self.connection:
            self.connection.executemany("""
                INSERT INTO games (
                    path, title, system, extension, artwork,
                    favorite, last_played
                )
                VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(path) DO UPDATE SET
                    title = excluded.title,
                    system = excluded.system,
                    extension = excluded.extension,
                    artwork = excluded.artwork
            """, rows)
        return len(rows)

    def list_games(self, filter_name: str | None = None,
                   limit: int | None = None, offset: int = 0):
        if filter_name == "favorites":
            sql = (f"SELECT {self._COLUMNS} FROM games WHERE favorite = 1 "
                   "ORDER BY title COLLATE NOCASE")
        elif filter_name == "recent":
            sql = (f"SELECT {self._COLUMNS} FROM games "
                   "WHERE last_played IS NOT NULL "
                   "ORDER BY last_played DESC")
        else:
            sql = (f"SELECT {self._COLUMNS} FROM games "
                   "ORDER BY title COLLATE NOCASE")

        parameters: tuple = ()
        if limit is not None:
            sql += " LIMIT ? OFFSET ?"
            parameters = (int(limit), int(offset))

        cursor = self.connection.execute(sql, parameters)
        return [dict(row) for row in cursor.fetchall()]

    def remove_missing(self, folder: str) -> int:
        """Drops rows whose file has disappeared from the scanned folder."""
        root = str(Path(folder).resolve())
        stale = [
            row["id"] for row in self.connection.execute(
                f"SELECT id, path FROM games WHERE path LIKE ?", (root + "%",)
            )
            if not Path(row["path"]).exists()
        ]
        if not stale:
            return 0

        with self.lock, self.connection:
            self.connection.executemany(
                "DELETE FROM games WHERE id = ?", [(i,) for i in stale]
            )
        return len(stale)

    def set_favorite(self, game_id: int, favorite: bool) -> bool:
        with self.lock, self.connection:
            cursor = self.connection.execute(
                "UPDATE games SET favorite = ? WHERE id = ?",
                (int(favorite), game_id),
            )
        return cursor.rowcount == 1

    def mark_played(self, game_id: int, timestamp: int) -> bool:
        with self.lock, self.connection:
            cursor = self.connection.execute(
                """
                UPDATE games
                SET last_played = ?, play_count = play_count + 1
                WHERE id = ?
                """,
                (timestamp, game_id),
            )
        return cursor.rowcount == 1

    def set_tags(self, game_id: int, tags: list[str]) -> bool:
        normalized = sorted({
            tag.strip().lower() for tag in tags if tag.strip()
        })
        with self.lock, self.connection:
            cursor = self.connection.execute(
                "UPDATE games SET tags = ? WHERE id = ?",
                (",".join(normalized), game_id),
            )
        return cursor.rowcount == 1

    def saves_directory(self) -> Path:
        """Where save blobs are kept, beside the database file."""
        folder = self.database_path.parent / "saves"
        folder.mkdir(parents=True, exist_ok=True)
        return folder

    def save_blob_path(self, game_id: int, kind: str) -> Path:
        # The caller validates `kind` against a whitelist before this
        # is reached, because it lands in a filename.
        return self.saves_directory() / f"{game_id}-{kind}.bin"

    def record_save(
        self, game_id: int, kind: str, digest: str, size: int,
        modified: int,
    ) -> bool:
        with self.lock, self.connection:
            self.connection.execute(
                "INSERT INTO saves (game_id, kind, digest, size, modified) "
                "VALUES (?, ?, ?, ?, ?) "
                "ON CONFLICT(game_id, kind) DO UPDATE SET "
                "digest = excluded.digest, size = excluded.size, "
                "modified = excluded.modified",
                (game_id, kind, digest, size, modified),
            )
        return True

    def list_saves(self, game_id: int) -> list[dict]:
        rows = self.connection.execute(
            "SELECT kind, digest, size, modified FROM saves "
            "WHERE game_id = ? ORDER BY kind",
            (game_id,),
        ).fetchall()
        return [dict(row) for row in rows]

    def get_save(self, game_id: int, kind: str):
        row = self.connection.execute(
            "SELECT kind, digest, size, modified FROM saves "
            "WHERE game_id = ? AND kind = ?",
            (game_id, kind),
        ).fetchone()
        return dict(row) if row else None

    def statistics(self):
        systems = self.connection.execute("""
            SELECT system, COUNT(*) AS count
            FROM games
            GROUP BY system
            ORDER BY system COLLATE NOCASE
        """).fetchall()
        row = self.connection.execute("""
            SELECT COUNT(*) AS total,
                   SUM(favorite) AS favorites,
                   SUM(last_played IS NOT NULL) AS recently_played,
                   SUM(play_count) AS plays
            FROM games
        """).fetchone()
        return {
            "total": row["total"] or 0,
            "favorites": row["favorites"] or 0,
            "recently_played": row["recently_played"] or 0,
            "plays": row["plays"] or 0,
            "systems": [
                {"system": item["system"], "count": item["count"]}
                for item in systems
            ],
        }

    def get_game(self, game_id: int):
        # An indexed lookup on the primary key, rather than the full table
        # scan the endpoint used to do.
        row = self.connection.execute(
            f"SELECT {self._COLUMNS} FROM games WHERE id = ?", (game_id,)
        ).fetchone()
        return dict(row) if row else None
