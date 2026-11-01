import sqlite3
from pathlib import Path
import threading

class GameDatabase:
    def __init__(self, database_path: str):
        self.database_path = Path(database_path)
        self.lock = threading.RLock()
        self.connection = sqlite3.connect(
            self.database_path,
            check_same_thread=False,
        )
        self.connection.row_factory = sqlite3.Row
        self.create_tables()

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
        self.connection.commit()

    def upsert_game(self, game: dict):
        with self.lock:
            self.connection.execute("""
            INSERT INTO games (
                path,
                title,
                system,
                extension,
                artwork,
                favorite,
                last_played
            )
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                title = excluded.title,
                system = excluded.system,
                extension = excluded.extension,
                artwork = excluded.artwork
            """, (
                game["path"], game["title"], game["system"],
                game["extension"], game.get("artwork"),
                int(game.get("favorite", False)), game.get("last_played"),
            ))
            self.connection.commit()

    def list_games(self):
        cursor = self.connection.execute("""
            SELECT
                id,
                path,
                title,
                system,
                extension,
                artwork,
                favorite,
                last_played,
                play_count,
                tags
            FROM games
            ORDER BY title COLLATE NOCASE
        """)

        return [dict(row) for row in cursor.fetchall()]

    def set_favorite(self, game_id: int, favorite: bool):
        with self.lock:
            cursor = self.connection.execute(
                "UPDATE games SET favorite = ? WHERE id = ?",
                (int(favorite), game_id),
            )
            self.connection.commit()
            return cursor.rowcount == 1

    def mark_played(self, game_id: int, timestamp: int):
        with self.lock:
            cursor = self.connection.execute(
                """
                UPDATE games
                SET last_played = ?, play_count = play_count + 1
                WHERE id = ?
                """,
                (timestamp, game_id),
            )
            self.connection.commit()
            return cursor.rowcount == 1

    def set_tags(self, game_id: int, tags: list[str]):
        normalized = sorted({
            tag.strip().lower() for tag in tags if tag.strip()
        })
        with self.lock:
            cursor = self.connection.execute(
                "UPDATE games SET tags = ? WHERE id = ?",
                (",".join(normalized), game_id),
            )
            self.connection.commit()
            return cursor.rowcount == 1

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
        row = self.connection.execute(
            "SELECT * FROM games WHERE id = ?", (game_id,)
        ).fetchone()
        return dict(row) if row else None