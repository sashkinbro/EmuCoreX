from __future__ import annotations

import json
import re
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAME_INDEX_PATH = ROOT / "app" / "src" / "main" / "cpp" / "assets" / "resources" / "GameIndex.yaml"
CATALOG_DB_PATH = ROOT / "app" / "src" / "main" / "assets" / "catalog" / "games.db"
OUTPUT_PATH = ROOT / "app" / "src" / "main" / "assets" / "catalog" / "pcsx2_compat_index.json"
IDENTITY_OUTPUT_PATH = ROOT / "app" / "src" / "main" / "assets" / "catalog" / "rom_identity_index.json"
OVERRIDES_PATH = ROOT / "app" / "src" / "main" / "assets" / "catalog" / "rom_identity_overrides.json"


def normalize_title(value: str | None) -> str | None:
    if not value:
        return None
    value = value.strip().lower()
    value = re.sub(r"[^\w]+", " ", value, flags=re.UNICODE)
    value = re.sub(r"\s+", " ", value).strip()
    return value or None


def compat_status(code: int | None) -> str:
    return {
        1: "NOTHING",
        2: "INTRO",
        3: "IN_GAME",
        4: "PLAYABLE",
        5: "PERFECT",
    }.get(code, "UNKNOWN")


def load_overrides() -> dict:
    if not OVERRIDES_PATH.exists():
        return {"serial_to_igdb": {}, "title_to_igdb": {}}
    data = json.loads(OVERRIDES_PATH.read_text(encoding="utf-8"))
    return {
        "serial_to_igdb": data.get("serial_to_igdb", {}) or {},
        "title_to_igdb": data.get("title_to_igdb", {}) or {},
    }


def parse_game_index() -> tuple[dict[str, dict], dict[str, dict], dict[str, list[dict]]]:
    serial_map: dict[str, dict] = {}
    title_map: dict[str, dict] = {}
    title_buckets: dict[str, list[dict]] = {}

    current_serial = None
    current_title = None
    current_region = None
    current_status = "UNKNOWN"

    def flush() -> None:
        nonlocal current_serial, current_title, current_region, current_status
        if not current_serial:
            return
        entry = {
            "serial": current_serial,
            "title": current_title,
            "region": current_region,
            "status": current_status,
        }
        serial_map[current_serial] = entry
        normalized = normalize_title(current_title)
        if normalized:
            title_buckets.setdefault(normalized, []).append(entry)
            existing = title_map.get(normalized)
            order = ["UNKNOWN", "NOTHING", "INTRO", "IN_GAME", "PLAYABLE", "PERFECT"]
            if existing is None or order.index(entry["status"]) > order.index(existing["status"]):
                title_map[normalized] = entry

    with GAME_INDEX_PATH.open("r", encoding="utf-8") as fh:
        for raw_line in fh:
            line = raw_line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            is_top_level = line and not line[0].isspace()
            trimmed = line.strip()
            if is_top_level and trimmed.endswith(":"):
                flush()
                current_serial = trimmed[:-1].strip().upper()
                current_title = None
                current_region = None
                current_status = "UNKNOWN"
                continue
            if current_serial is None:
                continue
            if trimmed.startswith("name-en:"):
                current_title = trimmed.split(":", 1)[1].strip().strip("\"'")
            elif trimmed.startswith("name:") and current_title is None:
                current_title = trimmed.split(":", 1)[1].strip().strip("\"'")
            elif trimmed.startswith("region:"):
                current_region = trimmed.split(":", 1)[1].strip().strip("\"'")
            elif trimmed.startswith("compat:"):
                code = trimmed.split(":", 1)[1].strip()
                current_status = compat_status(int(code)) if code.isdigit() else "UNKNOWN"

    flush()
    return serial_map, title_map, title_buckets


def build_index() -> dict:
    serial_map, title_map, _ = parse_game_index()
    igdb_map: dict[str, dict] = {}

    with sqlite3.connect(CATALOG_DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT g.igdb_id, g.name, MIN(s.serial) AS primary_serial
            FROM games g
            LEFT JOIN game_serials s ON s.igdb_id = g.igdb_id
            GROUP BY g.igdb_id, g.name
            """
        ).fetchall()

        for row in rows:
            igdb_id = str(row["igdb_id"])
            title = row["name"]
            serial = row["primary_serial"]
            entry = None
            matched_by = None

            if serial:
                entry = serial_map.get(serial.upper())
                if entry:
                    matched_by = "serial"

            if entry is None:
                entry = title_map.get(normalize_title(title))
                if entry:
                    matched_by = "title"

            if entry:
                igdb_map[igdb_id] = {
                    "serial": entry["serial"],
                    "title": entry["title"],
                    "region": entry["region"],
                    "status": entry["status"],
                    "matched_by": matched_by,
                }

    return {
        "version": 1,
        "igdb": igdb_map,
        "serial": serial_map,
        "title": title_map,
    }


def build_identity_index() -> dict:
    serial_map, _title_map, gameindex_title_buckets = parse_game_index()
    overrides = load_overrides()
    serial_to_igdb: dict[str, dict] = {}
    title_to_igdb: dict[str, dict] = {}
    db_title_buckets: dict[str, set[int]] = {}
    games: dict[str, dict] = {}

    with sqlite3.connect(CATALOG_DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT g.igdb_id, g.name, g.normalized_name, MIN(s.serial) AS primary_serial
            FROM games g
            LEFT JOIN game_serials s ON s.igdb_id = g.igdb_id
            GROUP BY g.igdb_id, g.name, g.normalized_name
            """
        ).fetchall()

        serial_rows = conn.execute(
            """
            SELECT igdb_id, serial
            FROM game_serials
            WHERE serial IS NOT NULL AND TRIM(serial) <> ''
            """
        ).fetchall()

        for row in rows:
            igdb_id = int(row["igdb_id"])
            normalized_title = normalize_title(row["normalized_name"] or row["name"])
            games[str(igdb_id)] = {
                "igdb_id": igdb_id,
                "name": row["name"],
                "normalized_title": normalized_title,
                "primary_serial": row["primary_serial"],
            }
            if normalized_title:
                db_title_buckets.setdefault(normalized_title, set()).add(igdb_id)

        for row in serial_rows:
            serial = (row["serial"] or "").strip().upper()
            if not serial:
                continue
            serial_to_igdb[serial] = {
                "igdb_id": int(row["igdb_id"]),
                "confidence": 10000,
                "matched_by": "serial_index",
            }

        for normalized_title, igdb_ids in db_title_buckets.items():
            if len(igdb_ids) != 1:
                continue
            igdb_id = next(iter(igdb_ids))
            title_to_igdb[normalized_title] = {
                "igdb_id": igdb_id,
                "confidence": 8500,
                "matched_by": "title_index",
            }

        for normalized_title, entries in gameindex_title_buckets.items():
            target = title_to_igdb.get(normalized_title)
            if not target:
                continue
            for entry in entries:
                serial = (entry.get("serial") or "").strip().upper()
                if not serial:
                    continue
                serial_to_igdb[serial] = {
                    "igdb_id": target["igdb_id"],
                    "confidence": 9200,
                    "matched_by": "serial_from_title_index",
                }
                game = games.get(str(target["igdb_id"]))
                if game is not None and not game.get("primary_serial"):
                    game["primary_serial"] = serial
                if game is not None and "pcsx2_compat" not in game:
                    game["pcsx2_compat"] = {
                        "serial": entry.get("serial"),
                        "title": entry.get("title"),
                        "region": entry.get("region"),
                        "status": entry.get("status"),
                    }

        for game in games.values():
            serial = (game.get("primary_serial") or "").strip().upper()
            compat = serial_map.get(serial)
            if compat:
                game["pcsx2_compat"] = compat

    for serial, entry in overrides["serial_to_igdb"].items():
        normalized_serial = serial.strip().upper()
        igdb_id = int(entry["igdb_id"])
        serial_to_igdb[normalized_serial] = {
            "igdb_id": igdb_id,
            "confidence": int(entry.get("confidence", 11000)),
            "matched_by": "override_serial",
        }
        game = games.get(str(igdb_id))
        if game is not None:
            game["primary_serial"] = normalized_serial

    for title, entry in overrides["title_to_igdb"].items():
        normalized_title = normalize_title(title)
        if not normalized_title:
            continue
        igdb_id = int(entry["igdb_id"])
        title_to_igdb[normalized_title] = {
            "igdb_id": igdb_id,
            "confidence": int(entry.get("confidence", 9500)),
            "matched_by": "override_title",
        }

    return {
        "version": 1,
        "serial_to_igdb": serial_to_igdb,
        "title_to_igdb": title_to_igdb,
        "games": games,
    }


def main() -> None:
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    payload = build_index()
    with OUTPUT_PATH.open("w", encoding="utf-8") as fh:
        json.dump(payload, fh, ensure_ascii=False, separators=(",", ":"))
    identity_payload = build_identity_index()
    with IDENTITY_OUTPUT_PATH.open("w", encoding="utf-8") as fh:
        json.dump(identity_payload, fh, ensure_ascii=False, separators=(",", ":"))
    print(
        f"Wrote {OUTPUT_PATH} with {len(payload['igdb'])} igdb matches; "
        f"{IDENTITY_OUTPUT_PATH} with {len(identity_payload['games'])} games"
    )


if __name__ == "__main__":
    main()
