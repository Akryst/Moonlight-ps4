"""Read Sunshine's local app names/covers; export a libconfig preview catalog."""
import argparse
import json
import hashlib
import struct
import zlib
from pathlib import Path, PureWindowsPath


def cover_path(value, source):
    if not isinstance(value, str) or not value:
        return ""
    win = PureWindowsPath(value)
    if win.drive and len(win.drive) == 2 and win.drive[1] == ":":
        candidate = Path("/mnt") / win.drive[0].lower() / Path(*win.parts[1:])
    else:
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = source.parent.parent / "assets" / candidate
    return str(candidate) if candidate.is_file() else ""


def landscape_cover(cover, cache):
    if not cover or Path(cover).stat().st_size > 16 * 1024 * 1024:
        return cover
    png = Path(cover).read_bytes()
    signature = b"\x89PNG\r\n\x1a\n"
    if not png.startswith(signature):
        return cover
    position = 8
    # Same optional mlBg PNG chunk emitted by steam_sync.cover_for and read by ui_art.c.
    while position + 12 <= len(png):
        length = struct.unpack_from(">I", png, position)[0]
        if length > len(png) - position - 12:
            break
        kind = png[position + 4:position + 8]
        payload = png[position + 8:position + 8 + length]
        checksum = struct.unpack_from(">I", png, position + 8 + length)[0]
        if kind == b"mlBg" and payload.startswith(signature) and zlib.crc32(kind + payload) == checksum:
            cache.mkdir(parents=True, exist_ok=True)
            target = cache / (hashlib.sha256(payload).hexdigest() + ".png")
            if not target.exists():
                temporary = target.with_suffix(".tmp")
                temporary.write_bytes(payload)
                temporary.replace(target)
            return str(target)
        if kind == b"IEND":
            break
        position += length + 12
    return cover


def export_catalog(source, output):
    if source.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Sunshine catalog exceeds 2 MiB")
    document = json.loads(source.read_text(encoding="utf-8-sig"))
    apps = document.get("apps") if isinstance(document, dict) else None
    if not isinstance(apps, list) or len(apps) > 4096:
        raise ValueError("Invalid Sunshine apps list")
    entries = []
    for app in apps:
        name = app.get("name") if isinstance(app, dict) else None
        if not isinstance(name, str) or not name.strip() or len(name.encode("utf-8")) > 512:
            raise ValueError("Invalid application name")
        if any(ord(c) < 32 or ord(c) == 127 for c in name):
            raise ValueError("Application name contains control characters")
        cover = cover_path(app.get("image-path"), source)
        cover = landscape_cover(cover, output.parent / "artwork")
        if len(cover.encode("utf-8")) > 4096:
            raise ValueError("Cover path exceeds limit")
        entries.append("{ name = %s; cover = %s; }" % (
            json.dumps(name, ensure_ascii=False), json.dumps(cover, ensure_ascii=False)))
    result = "apps = (\n" + ",\n".join(entries) + "\n);\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(result, encoding="utf-8")
    temporary.replace(output)
    return len(entries)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        count = export_catalog(args.source, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, "Catalog export failed: %s\n" % error)
    print("Sunshine local: %d applications imported (read-only)" % count)
