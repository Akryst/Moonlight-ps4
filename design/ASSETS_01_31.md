# Native UI artwork and typography

- `lake-background.png`: generated with the built-in imagegen tool, then embedded
  at 1280x720 by `scripts/embed_ui_background.py`. Used for Desktop and the
  Midnight/Cinema splashes. The original generated asset is preserved here.
- `native-01.33/*.png` and `native-themes-preview-01.33.png`: actual framebuffer
  output of the C renderer, converted to PNG for review. These are not AI mockups.
- Icons, rounded borders, glows and wave ribbons are drawn by native C code.
  Since 01.33, the splash uses Moonlight's eight-segment circular logo, matching
  the existing package icon `pkg/sce_sys/icon0.png`, with the theme's text color.
- The proportional font atlas is generated from DejaVu Sans regular/bold
  by `scripts/generate_ui_font.py`; copyright/license in `vendor/FONT-LICENSE.txt`.
  The source TTFs can be supplied from Ubuntu's fonts-dejavu-core package.

## Exact imagegen prompt

Create one production background asset for a PlayStation Moonlight streaming
client, 16:9 landscape. Photorealistic calm alpine lake at deep blue twilight/night,
silhouettes of forest and layered mountain ridges framing both sides, distant low
peaks centered on horizon at 65% of image height, subtle starlit navy sky taking
upper 60%, lake reflections in bottom 35%. Rich dark navy, muted cyan, faint warm
light at horizon. Elegant restrained cinematic atmosphere, no buildings, people,
moon, logos, text, symbols, interface or borders. Large clean dark sky space at
upper-middle for a crescent logo and white typography that will be drawn by the
application. Detailed landscape, soft natural light. It should match the lake and
mountain mood of the Midnight and Cinema UI concept splash screens.
