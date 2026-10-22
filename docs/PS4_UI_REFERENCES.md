# PS4 UI reference review — 2026-10-22

Read-only source review; none of these projects was built or run on the test PS4.
Research checkouts are under ignored `work/references/`. No dependency has been
added to Moonlight and the GPU experiment remains deferred.

| Project | Observed UI implementation | Relevance |
| --- | --- | --- |
| [Itemzflow](https://github.com/LightningMods/Itemzflow) | Custom C++ GLES2 UI; `orbisGlInit` and `orbisGlSwapBuffers`; links orbisGl and freetype-gl | Console-oriented game browser and artwork/navigation reference; not a standalone drop-in widget library |
| [PS4 Store](https://github.com/LightningMods/PS4-Store) | Custom GLES2 layouts, textures and atlas code; orbisGl presentation | Storefront layout reference; rendering/platform integration still needs adaptation |
| [Apollo PS4](https://github.com/bucanero/apollo-ps4) | C with SDL2 textures, copy/fill operations and an explicitly accelerated/vsynced renderer | Existing OpenOrbis SDL application reference; not a software-renderer example despite an old variable comment |
| [ezRemote Client](https://github.com/cy33hc/ps4-ezremote-client) | C++, Dear ImGui, SDL2 renderer backend, gamepad navigation; requests accelerated/vsynced SDL | Most direct example of an existing widget toolkit on PS4; visual style is primarily a file-management application |
| [pPlay](https://github.com/Cpasjuste/pplay) / [libcross2d](https://github.com/Cpasjuste/libcross2d) | C++ reusable 2D object/texture/text framework; PS4 selects SDL2 and GLES2; desktop targets also exist | Strongest candidate to evaluate for one UI source shared between PC and PS4 |
| [PKG Mutant Shop](https://github.com/XavyProd-Git/pkg-mutant-shop) | HTML/CSS/JavaScript page, HTTP backend and console browser launch route | Real web alternative, but a browser/server architecture rather than an embedded UI in a native video player |

## Source anchors

- Itemzflow: `itemzflow/source/main.cpp`, `itemzflow/Makefile`,
  `itemzflow/source/GLES2_ani.cpp`, `itemzflow/source/GLES2_filemanager.cpp`.
- PS4 Store: `Store/source/main.cpp`, `Store/source/GLES2_atlas.cpp`,
  `Store/Makefile`.
- Apollo: `source/main.c` (window/renderer initialization), `source/draw.c`.
- ezRemote: `source/main.cpp`, `source/imgui/imgui_impl_sdlrenderer.cpp`,
  `CMakeLists.txt`. Its web interface is distinct from its native ImGui interface.
- pPlay: `src/main.cpp`, `src/menus/menu.cpp`, `CMakeLists.txt`, `.gitmodules`.
- libcross2d: `CMakeLists.txt` (`PLATFORM_PS4`),
  `source/platforms/sdl2/sdl2_renderer.cpp`, and the
  [desktop/PS4 template instructions](https://github.com/Cpasjuste/libcross2d-template).
- Web example: `ARCHITECTURE.md`, `web/index.html`,
  `ps4-app/tile-pkg/README.md` in PKG Mutant Shop.

The [PacBrew SDL PS4 backend](https://github.com/PacBrew/SDL/tree/ps4/src/video/ps4)
is also relevant: `SDL_ps4piglet.c` initializes Piglet and `SDL_ps4opengles.c`
selects ES2 and delegates EGL context/swap operations. SDL is a higher-level
route with existing PS4 callers, not proof that Piglet/module/display constraints
disappear. Module and shader availability must be checked against the actual
selected build; our current clear/readback probe has not established shader
compilation or a working SDL renderer.

## Recommendation for Moonlight

Evaluate libcross2d as the first candidate for a console-style menu with desktop
development; use ezRemote as the reference if ready-made settings/list widgets
are the priority. Use Itemzflow/Store for navigation and presentation ideas rather
than importing either complete application. A separate browser UI can manage a
catalog, but browser-to-native streaming handoff remains an additional problem.

The next implementation should produce an actual menu preview on PC before
another PS4 package. Existing streaming, audio, decoder and VideoOut behavior
must remain intact. Cross2d is C++ while the client is C: compiler/runtime support,
integration boundaries, licenses and output ownership require assessment before
adopting code. No claim of drop-in compatibility or higher frame rate is made.

## Reviewed revisions

| Repository | Commit |
| --- | --- |
| LightningMods/Itemzflow | `4013e1ef188672db63346fef4221b1d8f4f13f02` |
| LightningMods/PS4-Store | `4d08c6e6a3d54d6f35a38a830f4ec8b045ed6c43` |
| bucanero/apollo-ps4 | `29da777575c288e677b342fa21b6cfc45283ef6a` |
| cy33hc/ps4-ezremote-client | `eb7387c79a8f644a9537502b2072ef60c0672250` |
| Cpasjuste/libcross2d | `9dfe2fcb4ccb2dd7be04718ee7ddb450abd85de1` |
| PacBrew/SDL (ps4) | `bf797a5315c7dbc0757d8ae7228e2dff1bf370cb` |
| XavyProd-Git/pkg-mutant-shop | `4354668db538e74ebc9be9ccbecc2d5ae2d225cd` |

pPlay pins libcross2d `4da9922626308aeea4fbad592d928c607950b68e`; the
separately reviewed current library is a different revision. Do not silently
replace its pinned dependency when reproducing a pPlay build.
