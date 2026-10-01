"""Publish installed Steam games to Sunshine without replacing manual apps.

Python 3, standard library; Pillow is optional for local Steam cover images.
Run --dry-run first or --apply to save. --safe-reload restarts Sunshine only
when its HTTP serverinfo reports no active application. Never reads Steam
account credentials or Sunshine web UI credentials.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import struct
import zlib
import urllib.request
import xml.etree.ElementTree as ET

MARKER = "moonlight-ps4-steam-id"


def parse_vdf(text: str) -> dict:
    tokens = []
    token = re.compile(r'\s+|//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])')
    pos = 0
    while pos < len(text):
        m = token.match(text, pos)
        if not m:
            raise ValueError(f"Invalid VDF token at offset {pos}")
        pos = m.end()
        if m.group(1) is not None:
            tokens.append((re.sub(r'\\([\\"])', r'\1', m.group(1)), False))
        elif m.group(2):
            tokens.append((m.group(2), True))
    cursor = 0

    def section(nested=False):
        nonlocal cursor
        out = {}
        while cursor < len(tokens):
            key, structural = tokens[cursor]
            cursor += 1
            if structural and key == "}":
                if nested:
                    return out
                raise ValueError("Unexpected closing brace")
            if structural or cursor >= len(tokens):
                raise ValueError("Missing VDF value")
            value, structural = tokens[cursor]
            cursor += 1
            if structural and value == "}":
                raise ValueError("Missing VDF value")
            out[key] = section(True) if structural and value == "{" else value
        if nested:
            raise ValueError("Unclosed VDF section")
        return out
    return section()


def steam_root() -> Path:
    if os.name == "nt":
        import winreg
        for hive, key, field in [
            (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
        ]:
            try:
                with winreg.OpenKey(hive, key) as handle:
                    return Path(winreg.QueryValueEx(handle, field)[0])
            except OSError:
                pass
    raise RuntimeError("Steam not found; pass --steam with its installation directory")


def scan_games(root: Path):
    folders = parse_vdf((root / "steamapps/libraryfolders.vdf").read_text(encoding="utf-8-sig"))
    libraries = [root]
    for key, value in folders.get("libraryfolders", {}).items():
        if key.isdigit():
            folder = value.get("path") if isinstance(value, dict) else value
            if folder and Path(folder) not in libraries:
                libraries.append(Path(folder))
    games, warnings = {}, []
    for library in libraries:
        manifests = library / "steamapps"
        if not manifests.is_dir():
            warnings.append(f"Unavailable Steam library: {library}")
            continue
        for file in sorted(manifests.glob("appmanifest_*.acf")):
            try:
                app = parse_vdf(file.read_text(encoding="utf-8-sig"))["AppState"]
                appid = str(app["appid"])
                if not appid.isdigit() or appid == "228980":
                    continue  # Steamworks Common Redistributables
                install = Path(app["installdir"])
                if install.is_absolute() or ".." in install.parts:
                    raise ValueError("Invalid Steam installation directory")
                if not (manifests / "common" / install).is_dir():
                    continue
                games[appid] = {"appid": appid, "name": str(app["name"])}
            except (KeyError, ValueError, OSError) as exc:
                warnings.append(f"Skipping malformed manifest {file.name}: {exc}")
    return sorted(games.values(), key=lambda g: (g["name"].casefold(), g["appid"])), warnings


def cover_for(root: Path, appid: str, output: Path) -> str:
    cache = root / "appcache/librarycache"
    choices = [cache / appid / "library_600x900.jpg", cache / f"{appid}_library_600x900.jpg",
               cache / appid / "library_header.jpg", cache / f"{appid}_header.jpg"]
    # New Steam clients store localized artwork under per-content hashes.
    for pattern in ("library_600x900*", "library_capsule*", "library_header*"):
        choices.extend(sorted(p for p in (cache / appid).rglob(pattern)
                              if p.suffix.lower() in (".jpg", ".jpeg", ".png")))
    source = next((p for p in choices if p.is_file()), None)
    if source is None:
        return "steam.png"
    try:
        from PIL import Image, ImageOps
        heroes = sorted(p for p in (cache / appid).rglob("library_hero*")
                        if p.suffix.lower() in (".jpg", ".jpeg", ".png") and "blur" not in p.name)
        hero = heroes[0] if heroes else None
        output.mkdir(parents=True, exist_ok=True)
        dest = output / f"steam-{appid}.png"
        outdated = not dest.exists() or max(source.stat().st_mtime, hero.stat().st_mtime if hero else 0) > dest.stat().st_mtime
        if not outdated:
            with Image.open(dest) as existing:
                outdated = existing.size != (600, 900)
            if hero and b"mlBg" not in dest.read_bytes():
                outdated = True
        if outdated:
            buffer = io.BytesIO()
            with Image.open(source) as image:
                ImageOps.fit(image.convert("RGB"), (600, 900)).save(buffer, "PNG")
            data = buffer.getvalue()
            if hero:
                panorama = io.BytesIO()
                with Image.open(hero) as image:
                    ImageOps.contain(image.convert("RGB"), (1600, 900)).save(panorama, "PNG")
                # A private, ancillary PNG chunk preserves the normal portrait
                # in other Moonlight clients. PS4 reads it for landscape cards.
                payload = panorama.getvalue()
                kind = b"mlBg"
                chunk = struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
                data = data[:-12] + chunk + data[-12:]
            write_atomic(dest, data)
        return str(dest.resolve())
    except (ImportError, OSError, ValueError):
        return "steam.png"


def merge_apps(document: dict, games: list, covers: dict, prune=True) -> dict:
    if not isinstance(document.get("apps"), list):
        raise ValueError("Sunshine apps.json must contain an apps array")
    installed = {g["appid"] for g in games}
    manual = [a for a in document["apps"] if MARKER not in a]
    managed = {str(a[MARKER]): dict(a) for a in document["apps"] if MARKER in a}
    represented = set()
    for app in manual:
        command = str(app.get("cmd", "")) + " " + " ".join(app.get("detached", []))
        represented.update(re.findall(r"steam://(?:rungameid|run)/(\d+)", command, re.IGNORECASE))
    added = []
    for game in games:
        appid = game["appid"]
        if appid in represented:
            continue
        app = managed.get(appid, {})
        app.update({"name": game["name"], MARKER: appid,
                    "cmd": f"steam://rungameid/{appid}", "auto-detach": True,
                    "wait-all": False, "image-path": covers.get(appid, "steam.png")})
        added.append(app)
    if not prune:
        added.extend(a for key, a in managed.items() if key not in installed)
    return {**document, "apps": manual + added}


def write_atomic(path: Path, data: bytes):
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def safe_reload(apps: Path, checksum: str):
    state = apps.with_name("moonlight-steam-loaded.sha256")
    if state.exists() and state.read_text().strip() == checksum:
        return "Already loaded"
    try:
        with urllib.request.urlopen("http://127.0.0.1:47989/serverinfo", timeout=3) as response:
            info = ET.fromstring(response.read())
        if info.findtext("currentgame") != "0" or info.findtext("state", "").endswith("BUSY"):
            return "Reload deferred: a Sunshine session is active"
        if os.name != "nt":
            return "Restart Sunshine to load the updated catalog"
        subprocess.run(["powershell.exe", "-NoProfile", "-WindowStyle", "Hidden",
                        "-Command", "Restart-Service SunshineService"], check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        write_atomic(state, checksum.encode("ascii"))
        return "Sunshine reloaded"
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as exc:
        return f"Catalog saved; reload deferred: {exc}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steam", type=Path)
    parser.add_argument("--apps", type=Path, default=Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Sunshine/config/apps.json")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--safe-reload", action="store_true")
    args = parser.parse_args()
    root = args.steam or steam_root()
    games, warnings = scan_games(root)
    document = json.loads(args.apps.read_text(encoding="utf-8-sig"))
    covers = {g["appid"]: cover_for(root, g["appid"], args.apps.parent / "moonlight-steam-covers")
              for g in games} if args.apply and not args.dry_run else {}
    merged = merge_apps(document, games, covers, prune=not warnings)
    print(json.dumps({"games": games, "warnings": warnings, "applications": len(merged["apps"])}, ensure_ascii=False, indent=2))
    if args.apply and not args.dry_run:
        data = (json.dumps(merged, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        if merged != document:
            backup = args.apps.with_name(f"apps.before-steam-{dt.datetime.now():%Y%m%d-%H%M%S-%f}.json")
            backup.write_bytes(args.apps.read_bytes())
            write_atomic(args.apps, data)
        if args.safe_reload:
            print(safe_reload(args.apps, hashlib.sha256(data).hexdigest()))


if __name__ == "__main__":
    main()
