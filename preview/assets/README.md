# Interface assets

`../../pkg/assets/ui/buttons/*.png` are original, transparent 128 × 128 RGBA drawings of PS4 control
symbols. They are loaded as textures, not drawn as vector glyphs at runtime.
Rebuild them with `python3 preview/make_button_assets.py` (Pillow required only
when rebuilding). They may be reused under this repository's license.

The keycaps use a raised rim, a recessed gradient face, a soft reflection and
embossed monochrome symbols. Their silhouettes remain readable at the console's
44-pixel prompt size. Regenerated PNGs must stay below the native loader's
32 KiB per-file limit; `preview/assets_check.cpp` checks this along with RGBA,
dimensions and transparency. All shading is baked into the images.

`DejaVuSans.ttf` is bundled unchanged for readable TV typography. Its copyright
and redistribution license are included in `FONT-LICENSE.txt`.

The same images are copied into the desktop build by CMake and into the PS4 PKG
as `/app0/assets/images/ps4-*.png`. The console uses alpha compositing to retain
transparent edges over every theme. No font file from this directory is needed
by the PS4 renderer, which already embeds its own proportional sans font.
