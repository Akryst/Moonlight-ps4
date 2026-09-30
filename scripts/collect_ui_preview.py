"""Convert actual framebuffer renders to reviewable PNGs (requires Pillow)."""
from pathlib import Path
import sys
from PIL import Image

source = Path(sys.argv[1])
root = Path(__file__).resolve().parents[1]
output = root / "design/native-01.33"
output.mkdir(parents=True, exist_ok=True)
names = ("midnight", "mono", "blue-wave", "daylight", "cinema")
gallery = Image.new("RGB", (1440, 2700))
for i, name in enumerate(names):
    for kind in ("theme", "splash", "settings"):
        with Image.open(source / f"{kind}-{i}.ppm") as image:
            image.save(output / f"{name}-{kind}.png")
            if kind == "theme":
                gallery.paste(image.resize((960, 540), Image.Resampling.LANCZOS), (0, i*540))
            else:
                gallery.paste(image.resize((480, 270), Image.Resampling.LANCZOS), (960, i*540 + (270 if kind == "settings" else 0)))
gallery.save(root / "design/native-themes-preview-01.33.png")
