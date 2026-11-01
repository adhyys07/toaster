from pathlib import Path
from typing import Iterator

SUPPORTED_EXTENSIONS = {
    ".nes": "NES",
    ".gb": "Game Boy",
    ".gbc": "Game Boy Color",
    ".cue": "PS1",
    ".chd": "PS1",
    ".iso": "PS2",
    ".cso": "PSP",
    ".pbp": "PSP",
    ".prx": "PSP",
    ".m3u": "PS1",
    ".ciso": "PSP",
    ".zso": "PSP",
    ".mdf": "PS1",
    ".nrg": "PS1",
    ".dump": "PS1",
    ".img": "PS1",
    ".ch8": "CHIP-8",
    ".c8": "CHIP-8",
    ".elf": "PS2",
}

def detect_system(path: Path) -> str:
    return SUPPORTED_EXTENSIONS.get(path.suffix.lower(), "Unknown")

def scan_rom_folder(folder: str) -> Iterator[dict]:
    root = Path(folder)

    if not root.exists():
        raise FileNotFoundError(f"ROM folder does not exist: {folder}")

    for path in root.rglob("*"):
        if not path.is_file():
            continue

        system = detect_system(path)

        if system == "Unknown":
            continue

        artwork = None
        for extension in (".png", ".jpg", ".jpeg", ".bmp"):
            candidate = path.with_suffix(extension)
            if candidate.exists():
                artwork = str(candidate)
                break

        yield {
            "path": str(path.resolve()),
            "title": path.stem,
            "system": system,
            "extension": path.suffix.lower(),
            "favorite": False,
            "last_played": None,
            "artwork": artwork,
        }