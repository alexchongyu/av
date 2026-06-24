# av 코드베이스 분석 (2026-06-24)

> 멀티 에이전트 워크플로우(11 agents, 7 subsystems) 산출물. 기능 업데이트 준비용 레퍼런스.


---

## 1. 아키텍처 개요

# Architecture Analysis: `av` Image Viewer & Comparator

`av` is a cross-platform, keyboard-first image viewer and comparison tool aimed at render engineers, artists, and researchers. Its defining capability is GPU-accelerated pixel-diff visualization between two images (plus side-by-side, overlay/swipe, false-color, and async SSIM heatmaps), with quantitative analysis (histograms, line cuts, statistics, scatter) and PNG/CSV export.

---

## 1. High-Level Architecture & Module Map

The codebase is organized as a single `AppState` god-struct threaded by reference through every subsystem, plus a parallel standalone C99 viewer that shares no code. Below is who-owns-what.

### Core / lifecycle / platform
| File | Owns |
|---|---|
| `src/main.cpp` | Process lifecycle: CLI parse, SDL3 window + OpenGL context creation (with software-renderer fallback), ImGui init, the per-frame event/slideshow/fit/render loop, procedural app icon, X11 error handler, `SdlCleanup` RAII. |
| `src/app.h` | `AppState` (the single mutable struct: 2 `ImageEntry`, 2 `ViewportState`, `DiffState`, all toggle/dialog flags, sequences, overlay, ROI, slideshow, copy-mode, fonts, `SDL_Window*`, `CliOptions`), plus all nested feature-state structs and enums. |
| `src/app.cpp` | `parse_cli` / `apply_cli_options`, the ~480-line `handle_keyboard` dispatcher, `sequence_navigate`, and `load_app_ini` / `save_app_ini` (`~/.av.ini`). |
| `src/render_backend.{h,cpp}` | Global `g_render_ctx` singleton (active backend enum + `SDL_Renderer*` or `SDL_GLContext`) and `is_software_mode()` — the branch predicate used throughout rendering. |
| `src/viewport.{h,cpp}` | Pure zoom/pan/fit math over `ViewportState`: `viewport_fit/zoom_in/zoom_out/set_zoom/pan/center/clamp_pan`, plus the `ZOOM_LEVELS[]` power-of-2 table. |

### UI layer (`src/ui/`)
| File | Owns |
|---|---|
| `main_window.cpp` | `MainWindow`: the full-screen host window, menu bar (File/View/Diff), 1/2/3-panel layout, all floating windows (image info, save dialogs, hotkey help, diff-pixel listing), foreground overlays (pixel balloon, zoom HUD, copy-mode banner, filename toast, panel borders). Also performs per-frame side effects (drain pending open/save, kick off SSIM, compute PSNR, set window title). |
| `statusbar.cpp` | `StatusBar`: the 24px bottom bar (zoom, dims, diff mode/direction/amplify, SSIM, PSNR, highlight count, ROI/overlay/slideshow/sync/FPS indicators). |
| `image_panel.cpp` | `ImagePanel`: per-panel image draw + all mouse interaction (pan, zoom, double-click, right-drag-to-zoom, crosshair, pixel overlays, magnifier, ROI, pathfinder minimap, context menu). Owns both GPU (FBO+shaders) and CPU (`cpu_render_*` into `SDL_Texture`) render paths. |
| `chart_windows.cpp` | On-screen ImGui histogram / hline / vline / stats / ROI-stats / scatter windows. |

### I/O pipeline
| File | Owns |
|---|---|
| `src/image_loader.{h,cpp}` | Decode dispatch (`load_image`: PPM-ASCII → PPM-binary → stb HDR/LDR), texture upload helpers, `free_image`, rotate, the LRU `ImageCache`, `scan_image_directory` (+ natural sort), and the unified funnel `load_image_and_populate_sequence`. |
| `src/image_open.{h,cpp}` | Native SDL open dialog (async callback) + the "Open Images" ImGui window. |
| `src/image_save.{h,cpp}` | `perform_save` dispatcher, PNG/BMP/PPM encoders, `compute_diff_cpu` (CPU mirror of the diff shader), context-menu save dialog. |
| `src/stb_impl.cpp` | Single TU instantiating `STB_IMAGE_*` / `STB_TRUETYPE_IMPLEMENTATION`. |
| `src/path_utils.h` | Header-only `path_last_sep` / `path_basename`. |

### Diff engine, GL, charts
| File | Owns |
|---|---|
| `src/diff_engine.{h,cpp}` | `DiffRenderer` (GPU diff via `DIFF_FRAG_SRC`), `SSIMComputer` (async `std::jthread`), `compute_ssim_cpu` (u8 + f32 variants). |
| `src/gl_texture.{h,cpp}` | `ShaderProgram`, `ScreenQuad`, `FBO` RAII wrappers, and `gl_upload_texture` / `_f32` / `_r32f` / `gl_delete_texture`. |
| `src/shader_sources.h` | The five GLSL `#version 150` string literals: `VERTEX_SRC`, `IMAGE_FRAG_SRC`, `DIFF_FRAG_SRC`, `SSIM_FRAG_SRC`, `BLEND_FRAG_SRC`. |
| `src/soft_renderer.{h,cpp}` | `SoftRenderer`: CPU RGBA8 canvas (primitives, stb_truetype text, `save_png`) used for chart export and software-mode output. |
| `src/chart_export.cpp` | Stat kernels (mean/variance/median/skew/kurtosis/entropy/RMS + MSE/PSNR/MAE/MaxError), `extract_*` (histogram/linecut/scatter), off-screen PNG + CSV export. |

### Image-out facilities
| File | Owns |
|---|---|
| `src/clipboard_image.{h,cpp}` | `clipboard_copy_image` — exports Image A/B/Diff to the system clipboard as PNG via SDL3's lazy clipboard-data callback API + in-memory stb PNG encode. |
| `src/av_x11.c` | A fully standalone C99 X11 viewer (see §2). Shares nothing with the SDL/ImGui app. |

---

## 2. The Two Build Targets

### Target 1 — `av` (main viewer), via `CMakeLists.txt`
- **Language/standard:** C++20 (`project(av C CXX)` — C is required because glad2 emits C sources).
- **Dependencies via FetchContent:** SDL3 (`release-3.2.0`, static-only), glad2 (`v2.0.6`, `gl:core=3.3`), Dear ImGui (`docking` branch, built as a STATIC lib with SDL3 + OpenGL3 + sdlrenderer3 backends), stb (header-only), and **optional** lcms2 (guarded by `AV_HAS_LCMS2`; build continues without it).
- **Per-platform:** macOS = arm64 only, deploy target 12.0, `-framework OpenGL/CoreFoundation`; Linux = dynamic OpenGL/X11 but `-static-libstdc++ -static-libgcc`, plus `NO_SHARED_MEMORY` on SDL3 (MIT-SHM is broken over SSH X11 forwarding); Windows = static MSVC CRT, GUI subsystem (no console), `STBI_WINDOWS_UTF8`.
- **Install:** Windows → `%LOCALAPPDATA%/av`; Unix → `~/.local/bin`. Output to `av/bin/`.
- **Purpose:** the full-featured GPU-accelerated viewer/comparator with the entire ImGui UI.

The CMake build also defines an `appimage` custom target, though the primary packaging path is the standalone script (§6).

### Target 2 — `av-x11`, via top-level `Makefile`
- **Language:** C99 (`src/av_x11.c`), **no SDL/CMake/ImGui** — only `-lX11 -lm` plus `stb_image.h` (auto-`curl`ed into `deps/`).
- **Purpose:** a minimal-dependency **fallback for ancient systems** (explicitly targeting CentOS 6.x — just gcc + libX11-devel) where the SDL/OpenGL stack won't run. It re-implements a stripped-down subset: load 1–2 images, pan/zoom, optional viewport sync, side-by-side compare, and a pixel-inspector status bar, all rendered into a CPU software framebuffer blitted with `XPutImage`. Output `bin/av-x11`.

**How they differ:** Target 1 is the canonical, feature-rich app (GPU diff, SSIM, charts, clipboard, all the analysis windows); Target 2 is a diagnostic/fallback binary with its own `main()`, its own `AvState`/`AvImage`/`AvViewport` structs, and zero shared code. The two are deliberately decoupled — `av_x11.c` is a portability lifeboat, not a component of the SDL app.

---

## 3. Runtime / Data Flow (launch → load → render loop → output)

### Startup (`main.cpp`)
1. `parse_cli` → `CliOptions`. `use_software` is decided from CLI and the `SSH_CONNECTION` env var **before** `SDL_Init`.
2. SDL window + GL context are attempted. Any failure (window, context, or `gladLoadGL`) flips `use_software` and falls through to the SDL software-renderer block. The chosen backend is published into the global `g_render_ctx`.
3. ImGui is initialized against whichever backend (GL3 vs SDLRenderer3) via `is_software_mode()`.
4. `AppState` is constructed → `load_app_ini` fills toggles → `apply_cli_options` overlays CLI → `state.window` set → fonts loaded → CLI images loaded via `load_image_and_populate_sequence`.

### Image load (single funnel)
All acquisition paths — CLI args, drag-drop, open dialog, sequence navigation — converge on **`load_image_and_populate_sequence(state, panel, path)`**. It `free`s the prior entry, calls `load_image`, resets the viewport to fit, invalidates PSNR (`psnr_computed=false`), rebuilds `state.sequences[panel]` via `scan_image_directory`, and sets the filename toast.

`load_image` is a try-chain: **PPM-ASCII → PPM-binary → stb** (`stbi_is_hdr` → `stbi_loadf` RGBA32F for HDR, else `stbi_load` forcing 4 channels). Each decoder fills the `ImageEntry` triple-buffer contract:
- `pixels` (RGBA8 display),
- `pixels_f32` (RGBA32F, only for HDR or PPM maxval>255),
- `pixels_orig` (uint16 RGB, PPM only) + `ppm_maxval`,

then uploads a texture whose handle type depends on `is_software_mode()`. The triple-buffer is what lets diff/PSNR/PPM-export use full bit depth.

> Note: the native open/save dialogs are **asynchronous** — SDL callbacks only set `*_path` + `*_pending` flags on `AppState`; the actual load/save happens at the **start of the next frame's render** (`MainWindow::render` drains them).

### Per-frame loop (`main.cpp`)
`SDL_PollEvent` → `ImGui_ImplSDL3_ProcessEvent` for each event. `KEY_DOWN` is gated by `!io.WantCaptureKeyboard || ctrl || gui`, then dispatched to `handle_keyboard`, which mutates `AppState`/viewports directly. `DROP_FILE` routes to the load funnel; `WINDOW_MOVED` sets a debounced (100ms) flag that skips rendering while moving. The slideshow countdown decrements by `io.DeltaTime` and calls `sequence_navigate`. Before drawing, `main` computes framebuffer size and, for each viewport with `.fit` set, calls `viewport_fit` using a per-panel width (`fb_w`, `/2`, or `/3` depending on both-loaded + diff mode).

### Render → output
`MainWindow::render(state)` (called between `ImGui::NewFrame()` and `ImGui::Render()`) builds the UI: host window → menubar → N `ImagePanel::render` calls → borders → statusbar → floating windows → balloon → zoom HUD. Then `main` does `ImGui::Render` + backend-specific clear/draw/present.

**GL texture path vs soft-renderer fallback** (selected by `is_software_mode()`):
- **GPU:** `ImagePanel` runs `fbo_.ensure/bind`, binds GL texture(s), sets shader uniforms from viewport+channel+diff params, draws the `ScreenQuad`, then `ImGui::Image(fbo_.tex_id)`.
- **CPU:** `cpu_render_*` reimplements the GLSL math per output pixel (inverse viewport transform, nearest at zoom≥1 / bilinear below, channel filter, ≥16x pixel grid) into `soft_buf_`, `SDL_UpdateTexture`, then `ImGui::Image(soft_texture_)`.

The two paths are near-complete copies of the same control flow, which is the largest structural duplication in the panel code.

---

## 4. The Diff Subsystem & Diff Modes

Two parallel diff implementations sit behind one logical `DiffState::Mode`:
- **GPU** — `DiffRenderer::render` (`diff_engine.cpp`) maps the mode to an int, binds A/B textures (resolved through `state.swap_images`), uploads ~13 uniforms (mode, amplify, channel, threshold, enhance min/max, alpha, image/view size, zoom, pan), and draws the fullscreen quad with `DIFF_FRAG_SRC` into an FBO. `image_panel.cpp::render_diff` is the caller; it also runs CPU Highlight-count / threshold post-processing for the status bar.
- **CPU** — `cpu_render_diff` (panel) and `compute_diff_cpu` (image_save, for disk/clipboard) mirror the same shader math.

**Diff modes** (keyboard-reachable; see §5 keybindings): `None` (Ctrl+D), `AlphaBlend` (Ctrl+2), `PixelAbsolute` (Ctrl+3), `PixelRelative` (Ctrl+4), `Enhance`/remap (Ctrl+5), `FalseColor` (Ctrl+6), `SSIM` (Ctrl+7), tolerance-diff toggle (Ctrl+8), `Highlight` (Ctrl+9). The diff-parameter keys `[` / `]` / `\` are **deliberately context-sensitive** to the active mode (amplify normally, alpha in AlphaBlend; `Shift+[`/`]` adjust threshold or alpha).

**SSIM** is computed off the render thread: `SSIMComputer::compute` copies both `ImageEntry`s and runs `compute_ssim_cpu` (11x11 σ=1.5 Gaussian; preferring `pixels_f32` over `pixels`) on a `std::jthread` with `std::stop_token` cancellation. The worker callback stores the result + heatmap; the **GPU upload of the heatmap is deferred to the main thread** (next frame), uploaded via `gl_upload_texture_r32f` (GPU) or converted to a false-color RGBA8 SDL texture (software). The diff/overlay paths are gated to `panel_idx==0`.

---

## 5. Charts/Histograms, Clipboard, Magnifier, Navigation

### Charts / histograms / export (`chart_export.cpp`, `chart_windows.cpp`)
Per-channel statistics (mean, variance, median, skewness, kurtosis, entropy, RMS, plus diff metrics MSE/PSNR/MAE/MaxError) computed by u8/f32 channel-stat kernels. Two parallel output paths over the same conceptual data:
- **On-screen** — six ImGui windows (histogram, hline cut, vline cut, stats, ROI-stats, scatter), rendered every frame and gated by `show_*` flags. (Histogram and line-cut windows currently re-implement binning/extraction inline rather than calling the `extract_*` functions — a documented divergence risk.)
- **Off-screen export** — `do_png`/`do_csv` drivers in `main_window.cpp` call `extract_*` + `export_*`; PNGs are drawn via the CPU `SoftRenderer` (log1p-scaled bars, axes, ticks) and CSVs via `std::ofstream`. Line-cut position follows the current `ViewportState` pan.

### Clipboard copy (`clipboard_image.cpp`)
Backend for the two-stage `Ctrl/Cmd+C → 1/2/3` copy mode. `clipboard_copy_image` resolves the target (0=A, 1=B, 2=Diff) against `swap_images`, selects RGBA8 bytes (direct `pixels`, f32→u8 conversion, or `compute_diff_cpu` for the diff), PNG-encodes in-memory into a heap `ClipboardPngData`, and hands ownership to `SDL_SetClipboardData` (lazy data/cleanup callbacks serve `image/png` on paste).

### Magnifier / overlays (`image_panel.cpp`)
`Ctrl+M`-toggled hover loupe (16x16 region at 32x cell), auto-hidden at zoom ≥32x; for diff panels it re-derives per-mode diff colors. Other per-panel overlays: crosshair (`M`), ≥32x per-pixel value grid, ROI rectangle (`Ctrl+E`), and the bottom-left "Pathfinder" minimap (`P`, modes thumbnail/schematic). Holding `Ctrl` with the magnifier active clamps the cursor to the image via `SDL_SetWindowMouseRect`.

### Navigation
- **Pan/zoom:** `h j k l`/arrows (Shift = ×5 fast, Cmd+Shift = jump to edge), `+`/`-`/`X`/`Z`, `1`–`8` = 2ⁿ scale, `0` fit, `F` fit toggle, `Space` 1:1, `G` center. Mouse: left-drag pan, right-drag drag-to-zoom (snaps to nearest power-of-2), wheel zoom, double-click zoom on cursor.
- **Sequence (directory):** `;` next / `a` prev in the active panel, `Tab` switches active panel A↔B, `Alt+Wheel` navigates the panel under the cursor; A/B browse independent directories with wrap-around and a 1.5s filename toast. Sequence navigation stages loads through the `open_state.open_pending` handoff consumed by `main_window.cpp` next frame.
- **`S`** toggles zoom/pan sync between panels; **`Shift+Space`** swaps A/B.

---

## 6. Cross-Cutting Concerns

### State & config (INI files)
Two INI files: **`~/.av.ini`** (`load_app_ini`/`save_app_ini` — UI toggle flags, `pixel_format`, `channel_mode`, border, magnifier; persisted on shutdown) and **`av_imgui.ini`** (ImGui layout, written relative to CWD via `io.IniFilename`). `AppState` is the single in-memory source of truth, passed by reference to virtually every function.

### Version injection (`cmake/GenVersion.cmake`)
Run every build (`av_gen_version ALL` target). Resolves version by priority: **(1) `git describe`** (`--tags --always --dirty --match 'v*'` + commit date) → **(2) `VERSION.txt`/`VERSION_DATE.txt`** (the offline path) → **(3) fallback `v0.22`** + timestamp. It writes only on content change (no spurious rebuilds): `build/generated/version.h` (`AV_VERSION`, `AV_VERSION_FULL`, `AV_VERSION_DATE`) for the binary, and `doc_typst/_version.typ` for the typst docs.

### Offline-dependency handling (`script/`)
- `fetch-deps.sh` (online machine): populates FetchContent sources → tars into **`av-deps.tar.gz`** (~88 MB, in repo); also bundles AppImage tooling for x86_64 + aarch64 → **`av-appimage-tools.tar.gz`** (~67 MB, in repo).
- `build-offline.sh`: extracts `av-deps.tar.gz` into `build/_deps/`, wipes stale cross-machine `CMakeCache.txt`, configures with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
- `build-linux.sh` / `build-static.sh` / `wsl-setup.sh` / `sync-linux.sh`: ordinary online build, fully-static build (CentOS 6.x glibc target), apt dep install, and rsync-to-offline-host (which writes `VERSION.txt`/`VERSION_DATE.txt` so GenVersion works without `.git`).

### Packaging
- `make-appimage.sh` (primary): builds (prefers offline), bakes the version into the output filename `av-<ver>-<arch>.AppImage`, runs linuxdeploy to collect libs, **bundles Mesa `swrast_dri.so`** for GPU-less environments, bundles core glibc + `ld-linux`, and writes a custom AppRun that **defaults to `LIBGL_ALWAYS_SOFTWARE=1`** (override via `AV_HARDWARE_GL=1`) and execs through the bundled loader.
- `bundle-av.sh`: lighter sudo-free alternative (copies `av` + `ldd` libs + ELF interpreter into a self-contained dir with a wrapper).
- `start-vnc.sh`: a TigerVNC testing aid (headless display for testing `av`), not a packaging step.

> Latent gap noted in build meta: `build-static.sh` references `-DAV_FULL_STATIC=ON`, but `AV_FULL_STATIC` is **not handled in the current `CMakeLists.txt`** — only `-static-libstdc++ -static-libgcc` is wired up, so true full-static linking isn't implemented in CMake.

---

## 7. Clearest Seams / Extension Points

The codebase has well-trodden extension patterns (codified in `tasks/lessons.md`). The cleanest seams:

| To add… | Touch | Pattern |
|---|---|---|
| **A keyboard command** | `app.cpp handle_keyboard()` switch | Add a `SDL_SCANCODE_*` case, read modifiers, mutate `AppState`/`viewport_*`. Ensure the `main.cpp` gate (`!WantCaptureKeyboard \|\| ctrl \|\| gui`) lets global shortcuts through. |
| **A persistent setting** | `app.cpp load_app_ini`/`save_app_ini` + an `AppState` field | Add a matching `key==` branch in load and a `f<<"key="` line in save. |
| **A CLI option** | `parse_cli` + `apply_cli_options` + `CliOptions` | Add the field, a `parse_cli` branch (use the `next()` lambda), document in `print_help`, copy into `AppState`. |
| **A feature-state container** | `AppState` struct in `app.h` | Add a nested struct + member; it's automatically visible everywhere since `AppState` is passed by reference. |
| **A GPU diff mode** | `DiffState::Mode` enum + `DIFF_FRAG_SRC` + `DiffRenderer::render` mode switch + `cpu_render_diff` + magnifier color branch | The same mode logic is duplicated in 3–4 places (GPU shader, CPU panel, magnifier, save), so each new mode touches all of them. |
| **An overlay/composite mode** | `OverlayState::Mode` + `BLEND_FRAG_SRC` `u_blend_mode` case + `cpu_render_overlay` | Handle drag interaction in both GPU and software overlay functions. |
| **An input format** | `load_image` try-chain + `SUPPORTED_IMG_EXTS` + open-dialog filter string | Three edits because the extension list is duplicated; new `try_load_<fmt>()` fills the buffer triple and uploads via existing helpers. |
| **A save/encode format** | `ImageSaveDialog::Format` + `save_<fmt>_impl` + both `perform_save` switches | The format dispatch is duplicated between the A/B branch and the Diff branch. |
| **An analysis window / chart** | `chart_export` data+extractor → `chart_windows` render fn → `show_*` flag in `AppState` → `app.cpp` hotkey → `main_window.cpp` menu item + render-loop call | The canonical "new analysis" recipe in `lessons.md`. |
| **A menu item / status indicator** | `render_menubar` / `StatusBar::render` | Append following the existing `'\|'`-separated pattern. For diff modes, also update `statusbar diff_mode_name` and `title_diff_label`. |
| **A per-panel HUD/overlay or toast** | tail of `render_single`/`render_diff`/`render_overlay` (+ their software twins); foreground overlays via `GetForegroundDrawList()` | Templates: `render_pathfinder`/`render_magnifier`, `render_copy_mode_overlay`/`render_filename_toast`. |
| **A clipboard MIME/target** | `clipboard_data_callback` + `mime_types[]` + `clipboard_copy_image` dispatch | SDL can advertise multiple types in one registration. |
| **Caching for sequence nav** | wire the already-implemented but currently-unused `g_image_cache` (LRU, `MAX_ENTRIES=8`) into the load funnel. |

**Architectural cautions that constrain extension:**
- The **screen↔image inverse transform** (`img = (screen - half_view)/zoom - pan + half_img`) is hand-inlined in ~10+ functions across `image_panel.cpp`; any change to the convention must be edited everywhere. This is the single largest fragility in the panel code, and the same math is re-derived independently in the `main_window.cpp` pixel balloon.
- The **GPU and CPU render paths** in `image_panel.cpp` are near-duplicate twins; logic drift between them is a known hazard (e.g. software AlphaBlend ignoring `diff.alpha`).
- The **panel-count layout rule** (`fb_w`, `/2`, `/3`) is encoded in both `main.cpp` (fit-zoom) and `image_panel.cpp` (draw rect); a layout change requires edits in both.
- The **diff-mode set is inconsistent** across the Diff menu, hotkey-help table, `statusbar diff_mode_name`, and `title_diff_label` — AlphaBlend/Enhance are reachable by key but missing or mislabeled in some of these surfaces.
- `render_backend` dispatch is a scattered 2-way `if (is_software_mode())` branch (not virtual), so a third backend would touch every such site.

---

*Files referenced are all under `/Users/alex/works/github/github.com/claude_code/av`. The most authoritative as-shipped reference for behavior is `doc_typst/av_cheat_sheet.typ` (v0.22); the original `doc_typst/av-spec.typ` (v0.1) has diverged from the implementation and should be treated as design intent, not current truth.*


---

## 2. 빌드/문서 메타

I have read all the requested files in full. Here is the dense summary.

---

# av Project — Build, Packaging, Spec, Keybindings, TODOs, Lessons

## Two build targets and how they differ

**Target 1 — `av` (main viewer)** via `CMakeLists.txt`. C++20, `project(av C CXX)` (C is required because glad2 generates C sources). Pulls all deps via CMake FetchContent: SDL3 (`release-3.2.0`, static only, `SDL_SHARED OFF`/`SDL_STATIC ON`), glad2 (`v2.0.6`, `gl:core=3.3`), Dear ImGui (`docking` branch, custom STATIC lib of 7 .cpp incl. SDL3 + OpenGL3 + sdlrenderer3 backends), stb (master, header-only), and **optional** lcms2 (pkg-config or find_package; build continues without it, guarded by `AV_HAS_LCMS2`). 16 source files under `src/` + `src/ui/`. Output goes to `av/bin/`. Per-platform: macOS = arm64 only, deploy target 12.0, `-framework OpenGL/CoreFoundation`; Linux = dynamic OpenGL/X11 but `-static-libstdc++ -static-libgcc` for old-distro portability, plus `NO_SHARED_MEMORY` on SDL3-static (MIT-SHM broken over SSH X11 forwarding); Windows = static MSVC CRT, GUI subsystem (no console), `STBI_WINDOWS_UTF8`. Install: Windows → `%LOCALAPPDATA%/av`; Unix → `~/.local/bin`.

**Target 2 — `av-x11`** via top-level `Makefile`. A completely standalone **C99** X11 viewer (`src/av_x11.c`), no SDL/CMake/ImGui — just `-lX11 -lm` plus stb_image.h (auto-curled into `deps/`). Explicitly aimed at **CentOS 6.x compatibility** (just gcc + libX11-devel). Output `bin/av-x11`. This is the minimal-dependency fallback for ancient systems where the SDL/OpenGL stack won't run.

The CMake build also defines an `appimage` custom target (auto-downloads `linuxdeploy-<arch>.AppImage`, builds AppImage to `bin/av-<arch>.AppImage`), but the richer packaging path is the standalone script (below).

## Dependency & version handling (offline-friendly)

**Version injection** (`cmake/GenVersion.cmake`, run every build via `av_gen_version ALL` target): resolves version with priority **(1) git describe** (`--tags --always --dirty --match 'v*'`, plus commit date from `git log -1 --format=%cs`) → **(2) `VERSION.txt`/`VERSION_DATE.txt`** files (the offline-sync path) → **(3) fallback `v0.22`** + current timestamp. It writes two files only when content changed (no spurious rebuilds): `build/generated/version.h` (`AV_VERSION` short e.g. "0.22", `AV_VERSION_FULL`, `AV_VERSION_DATE`) consumed by the binary, and `doc_typst/_version.typ` (`AV_VERSION`, `AV_VERSION_SHORT`, `AV_VERSION_DATE`) consumed by the typst docs. Short form strips leading `v` and the `-N-gHASH` suffix. Current `_version.typ` shows `v0.22-3-gf7d6a3e-dirty` / short `0.22` / date `2026-04-23`.

**Offline dependency flow:**
- `script/fetch-deps.sh` (run on an online machine): configures a throwaway CMake build to populate FetchContent sources, then tars `sdl3-src glad-src imgui-src stb-src` → **`av-deps.tar.gz`** (~88 MB, present in repo). Also downloads AppImage tooling for both x86_64 and aarch64 (linuxdeploy, appimagetool, type2 runtime) → **`av-appimage-tools.tar.gz`** (~67 MB, present in repo).
- `script/build-offline.sh`: requires `av-deps.tar.gz`, extracts into `build/_deps/`, detects/wipes stale `CMakeCache.txt` from another machine (compares `CMAKE_HOME_DIRECTORY`), configures with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`, builds.
- `script/build-linux.sh`: ordinary online Linux/WSL build (`debug`/`clean` args). Pre-checks cmake/git/libgl, detects WSL + DISPLAY.
- `script/build-static.sh`: fully static build (`-DAV_FULL_STATIC=ON`, separate `build-static/` dir) targeting old glibc (CentOS 6.x); auto offline if archive present; reports `ldd` + required GLIBC symbol versions. (Note: `AV_FULL_STATIC` is referenced by the script but is **not** actually handled in the current `CMakeLists.txt` — only `-static-libstdc++ -static-libgcc` is wired up, so true full-static linking isn't implemented in CMake.)
- `script/wsl-setup.sh`: installs apt build deps (build-essential, cmake, git, pkg-config, libgl/egl/x11/wayland/xkbcommon/dbus/ibus/lcms2 dev), checks CMake ≥3.24 (offers snap upgrade).
- `script/sync-linux.sh`: rsync source tree to an offline Linux host (`AV_SYNC_REMOTE`, default `192.168.2.2:/user/alex/claude_code/av/`), writing `VERSION.txt`/`VERSION_DATE.txt` snapshots so GenVersion resolves correctly without `.git`; excludes build/bin/.git/test/deps/archives; removes the VERSION files locally afterward (git is source of truth). One-time prep: `rm -rf .git` on the Linux side.

## Packaging / AppImage flow

`script/make-appimage.sh` is the primary packaging path (self-contained AppImage). Steps: [1/5] build (prefers `build-offline.sh` if `av-deps.tar.gz` exists, else `build-linux.sh`; `--no-build` skips); extracts version via `bin/av --version` and **bakes it into the output filename** `av-<ver>-<arch>.AppImage`. [2/5] obtains linuxdeploy/appimagetool/runtime — existing file → extract from `av-appimage-tools.tar.gz` → curl download (in that order, for offline machines). [3/5] runs linuxdeploy to build AppDir and collect shared libs. [4/5] **bundles Mesa `swrast_dri.so`** (+ `kms_swrast_dri.so`) searched across distro lib paths — software rendering for GPU-less/VM/X11-forwarding environments. [5/5] bundles core glibc libs (`libc/libm/libdl/libpthread/librt`) + `ld-linux`, writes a **custom AppRun** that: sets `APPIMAGE_EXTRACT_AND_RUN=1` (FUSE-less), prepends bundled `usr/lib` to `LD_LIBRARY_PATH`, sets `LIBGL_DRIVERS_PATH`, **defaults to `LIBGL_ALWAYS_SOFTWARE=1`** (override with `AV_HARDWARE_GL=1` for GPU), and execs through the bundled `ld-linux` if present. Packaged with appimagetool `--runtime-file`.

`script/bundle-av.sh` is a lighter, sudo-free alternative: copies `bin/av` + its `ldd` libs + the ELF interpreter into `bin/av-bundle/` with a wrapper script that execs via the bundled `ld-linux-x86-64.so.2`. Tar it and run on any machine.

`script/start-vnc.sh` (testing aid, not packaging): boots TigerVNC `Xvnc :1` on port 5901 (pw `avview`), extracting tigervnc `.deb`s into `~/local` if needed; generates the VNC passwd via `vncpasswd` with an **openssl DES-ECB fallback** (fixed key `e84ad660c4721ae0`), tries `-query localhost` (GDM XDMCP) then falls back to a direct gnome/openbox/xterm session — used to give the headless Linux box a display for testing `av`.

## Documented feature spec (`av-spec.typ`, v0.1, pre-implementation)

Cross-platform image **viewer + comparison tool** for render engineers/artists/researchers; GPU-accelerated diff, keyboard-first. Design principles: keyboard-first, zero-latency GPU diff, lightweight/fast startup, accurate ICC color, multi-monitor (native windows), simple architecture. Platforms: macOS 12+, Win 10/11 x64, Linux X11/Wayland glibc≥2.31. Stack: SDL3 + Dear ImGui (docking) + OpenGL 3.3 Core + glad2 + stb_image + lcms2, C++20 (`std::jthread`). Supported formats: PNG (8/16-bit), JPEG (EXIF orient), BMP, TGA, HDR (RGBE→float), PIC, PNM/PBM/PGM/PPM. Image cache: last **8** images (LRU), GPU textures freed on swap, async load for ≥4096². State structs: `ImageEntry`, `ViewportState` (zoom 0.1–32×, pan, fit), `DiffState` (Mode {None, PixelAbsolute, PixelRelative, SSIM}, amplify, ssim_score), `AppState` (2 images/views, diff, sync_viewports, show_histogram). Compare modes: Side-by-Side, Overlay/Swipe, Diff, SSIM heatmap. Diff engine: GPU fragment shader (abs / relative / component, amplify uniform) + async CPU **SSIM** (11×11 Gaussian, jthread, heatmap). Roadmap = 5 phases/milestones (M1 foundation → M2 viewport → M3 diff → M4 color mgmt → M5 release: CLI, settings, file-watch, ≥60% unit coverage, macOS/Win/Linux packages). NOTE: the spec is the **original design intent and has diverged** from the shipped app (see cheat sheet below — real keybindings, diff-mode set, and CLI flags differ substantially; e.g. spec says `Ctrl+1..6`, app uses `Ctrl+2..9`; spec `--diff-mode` modes vs. actual; SSIM/color-mgmt may be partial).

## Full keybinding map (`av_cheat_sheet.typ` — the authoritative, as-shipped v0.22 set)

**CLI:** `av [image_a] [image_b] [options]` — `--diff-mode abs|rel|falsecolor|ssim`, `--zoom fit|1|2|…`, `--amplify 0.1~100`, `--sync`/`--no-sync`, `--fullscreen`, `--geometry WxH`, `--software`, `--windowed`, `-nb` (no border), `--profile <icc>`, `--no-color-mgmt`, `-p <N>` (pan step px), `-bc <A> <B> <D>` (panel border colors hex). `--version`/`-h` also exist.

**Zoom:** `+`/`-` zoom; `Z`/`Shift+Z` zoom alt; `X` zoom out; `0` fit-to-window; `1`–`8` = 2ⁿ scale (1×–256×); `F` fit toggle; `Space` 1:1 (100%) toggle.

**Pan:** `h j k l` = left/down/up/right; arrow keys; `Shift`+move = fast (×5); `Cmd+Shift+H/J/K/L` = jump to edge; `G` center image.

**Diff modes:** `Ctrl+D` None(off); `Ctrl+2` Alpha Blend (A·B mix); `Ctrl+3` Absolute; `Ctrl+4` Relative; `Ctrl+5` Enhance(remap); `Ctrl+6` False color; `Ctrl+7` SSIM; `Ctrl+8` Tolerance-diff toggle; `Ctrl+9` Highlight; `Ctrl+\` threshold reset.

**Diff primary param (mode-sensitive):** `[`/`]` = amplify −0.5/+0.5 (in AlphaBlend: alpha ∓1%); `Shift+[`/`Shift+]` = threshold ∓1 (AlphaBlend: alpha ∓10%); `\` = amplify reset (AlphaBlend: alpha=50%).

**Channels:** `Shift+R/G/B` = isolate R/G/B; `Shift+C` = RGB reset.

**Display toggles:** `U` UI; `I` info panel; `V` pixel-value tooltip; `Ctrl+X` cycle pixel format (Dec → 0xHex → Hexh, persisted to av.ini); `P` Pathfinder; `Ctrl+P` Schematic.

**Analysis:** `Ctrl+H` histogram; `Ctrl+L` horizontal cut; `Ctrl+Y` vertical cut; `Ctrl+S` statistics; `Ctrl+E` ROI select; `Ctrl+T` scatter plot; `M` crosshair; `Shift+A` slideshow autoplay; `Shift+↑`/`Shift+↓` slideshow interval.

**Overlay:** `O` overlay toggle.

**Sequence nav (directory):** `;` next image (active panel); `a` prev image; `Tab` switch active panel A↔B (border-highlight); `Alt+Wheel` navigate panel under cursor; `N`/`Shift+N` next/prev (legacy). Filename toast 1.5 s on load, wraps around; A/B panels browse independent directories.

**File:** `Shift+Ctrl+O` open; `Shift+Ctrl+S` save screenshot; `Q` quit.

**Clipboard copy (two-stage):** `Ctrl/Cmd+C` then `1`/`2`/`3` = copy Image A / B / Diff as PNG; `Esc` or 5 s timeout cancels.

**Misc:** `W` windowed-mode toggle (titlebar); `B` panel border toggle (persisted av.ini); `S` zoom/pan sync toggle; `Shift+Space` swap A/B; `R` rotate 90°; `Ctrl+R` rotate reverse; `Ctrl+Shift+H` hotkey-help toggle.

**Mouse:** left-drag pan; right-drag drag-to-zoom; wheel zoom; ROI-mode+drag region select; hold `Ctrl` (magnifier on) clamps cursor to image.

**Magnifier:** `Ctrl+M` toggle (persisted av.ini); hover = 16×16 area at 32× tooltip; auto-hides at zoom ≥32×.

Persistence files: `av.ini` (border, pixel_format, magnifier) and `av_imgui.ini` (ImGui layout), both in repo root.

## Outstanding TODOs

`tasks/todo.md` contains **only completed** sections (no open items): spec authoring, Phase 1–3 core implementation, Phase 4 (side-by-side, UI toggle, `-d` diff flag), "Advanced Pixel Lens" rename + procedural app icon + Pathfinder + border-clipping fix, Phase 5 (ROI stats, sequence nav, Overlay/Blend, scatter plot). No tracked open work items remain in todo.md. The `plans/` directory holds per-feature design docs, the most recent being Alpha Blending (Ctrl+2, v0.22), "suppress Identical overlay in AlphaBlend" bugfix, and image-directory navigation (v0.21) — all appear **shipped** per recent git log (commits through v0.22). The spec→implementation drift itself is an implicit open item (spec is stale relative to the cheat sheet). Note the build-static.sh `AV_FULL_STATIC` flag has no CMake implementation — a latent gap.

## Lessons-learned that constrain future changes (`tasks/lessons.md`)

- **Struct relocation across anonymous namespaces:** when moving structs/static compute fns from `chart_windows.cpp` to `chart_export.h/.cpp`, define structs in the global namespace in the header, remove the anon-namespace duplicates, keep only `draw_stats_table` in the anon namespace, include `chart_export.h` at top of `chart_windows.cpp` (avoids ODR/duplicate-definition errors).
- **stb single-implementation rule:** `STB_TRUETYPE_IMPLEMENTATION` (and image_write) lives **only** in `stb_impl.cpp`; other TUs (e.g. `soft_renderer.cpp`) include the header **without** the IMPLEMENTATION macro.
- **SDL async file dialogs:** `SDL_ShowOpen/SaveFileDialog` are callback-based; heap-allocate the userdata struct with `new`, `delete` in the callback; set `pending_path` only inside the callback and do the real work at the **start of the next frame's render()**.
- **ImGui font stack:** `PopFont` must run even when `Begin()` returns false (collapsed window) — always pair PushFont→Begin→…→End→PopFont regardless of Begin's result.
- **CMake sources:** every new `.cpp` must be added to `AV_SOURCES`; target compile-defs (e.g. `IMGUI_FONT_DIR`) propagate to all av sources.
- **ImGui ID collisions:** duplicate button labels collide — use `##suffix` for unique IDs (e.g. `"PNG A##ha"`).
- **Established extension patterns (2026-03-05):** add a state struct to `app.h` (with default values) → field on `AppState`; new analysis window = chart_export data+extract fn → chart_windows render decl/impl → `show_xxx` bool → app.cpp hotkey → main_window.cpp menu item + render call; new render mode = new GLSL in `shader_sources.h` → ShaderProgram member + render method in image_panel → compile in init() → conditional dispatch in render() → layout branch in main_window; ROI = in ROI mode left-drag selects instead of pans, screen↔image-pixel conversion via existing `s2ix/s2iy`; sequence nav = auto `scan_image_directory()` on load → N/Shift+N adjust `current_index` → reuse `open_state.open_pending` (with `clear_other=false` to preserve the other slot).

Additional implicit conventions from recent plans worth honoring: diff-parameter keys `[`/`]`/`\` are deliberately **context-sensitive to the active diff mode** (amplify normally, alpha in AlphaBlend); a shared `load_image_and_populate_sequence()` helper is the single funnel for CLI/drop/dialog loads (CLI and drag-drop previously skipped `scan_image_directory`, breaking sequence nav — fixed); clipboard logic was extracted to `src/clipboard_image.{h,cpp}` (platform-independent: SDL3 `SDL_SetClipboardData` + stb_image_write); every feature plan ends with a typst doc update (cheat_sheet + av-app-implementation) + version bump.


---

## 3. 기능 카탈로그 (29)

- **Image viewing (single image)** — Open and display one image with GPU or software-rendered pan/zoom, fit-to-window, and 1:1 modes.
  - `src/ui/image_panel.cpp ImagePanel::render_single / render_single_software; src/viewport.cpp viewport_fit`
- **Side-by-side comparison** — Display Image A and Image B in two panels, optionally with synchronized pan/zoom across both.
  - `src/ui/main_window.cpp MainWindow::render panel layout (lines ~1435-1490); sync via src/app.cpp handle_keyboard SDL_SCANCODE_S`
- **GPU pixel-diff modes** — Real-time fragment-shader diff visualizations: Absolute, Relative, FalseColor, Enhance, Highlight, AlphaBlend (Ctrl+2..9).
  - `src/diff_engine.cpp DiffRenderer::render; src/shader_sources.h DIFF_FRAG_SRC; src/app.cpp handle_keyboard Ctrl+digit cases`
- **SSIM structural-similarity heatmap** — Async CPU SSIM (11x11 Gaussian) producing a score plus a false-color dissimilarity heatmap uploaded as a texture.
  - `src/diff_engine.cpp SSIMComputer::compute / compute_ssim_cpu; consumed in src/ui/main_window.cpp (~1287-1349)`
- **Overlay / curtain compositing** — Blend or swipe-curtain compositing of A over B, with left-drag to move the curtain divider (O key).
  - `src/ui/image_panel.cpp render_overlay / render_overlay_software; src/shader_sources.h BLEND_FRAG_SRC`
- **Hover magnifier loupe** — 16x16 pixel region shown at 32x with grid, coordinate, and per-mode RGB/diff values; auto-hides at zoom >=32 (Ctrl+M).
  - `src/ui/image_panel.cpp ImagePanel::render_magnifier (line 1413)`
- **Per-pixel value inspection** — Crosshair, pixel-value tooltip balloon, and >=32x per-pixel RGB/diff grid overlays with Dec/Hex format cycling.
  - `src/ui/image_panel.cpp render_crosshair / render_pixel_values / render_diff_pixel_values; balloon in src/ui/main_window.cpp (1591-1802)`
- **ROI selection and statistics** — Ctrl+E enters ROI mode; left-drag selects a rectangle and opens per-channel statistics for that region.
  - `src/ui/image_panel.cpp handle_roi_drag / render_roi_overlay; src/chart_export.cpp compute_roi_stats; src/ui/chart_windows.cpp render_roi_stats_window`
- **Histograms and line cuts** — 256-bin RGB histograms and horizontal/vertical line-cut profile charts rendered interactively (Ctrl+H/L/Y).
  - `src/ui/chart_windows.cpp render_histogram_window / render_hline_cut_window / render_vline_cut_window`
- **Image statistics panel** — Per-channel mean/variance/median/skew/kurtosis/entropy plus diff MSE/PSNR/MAE/MaxError tables (Ctrl+S).
  - `src/chart_export.cpp compute_image_stats / compute_diff_stats; src/ui/chart_windows.cpp render_stats_window`
- **Scatter plot (A vs B)** — Per-channel scatter of sampled A/B pixel pairs with a diagonal match line (Ctrl+T).
  - `src/chart_export.cpp extract_scatter_plot; src/ui/chart_windows.cpp render_scatter_plot_window`
- **Differing-pixel listing** — Paginated, channel-filtered table of differing pixels with 'go to pixel #' that jumps both views to 32x centered.
  - `src/ui/main_window.cpp render_diff_listing_window (lines 1002-1219)`
- **Chart/stats export (PNG + CSV)** — Headless export of histograms, line cuts, and statistics to PNG (CPU SoftRenderer) and CSV, with separate-channel splitting.
  - `src/chart_export.cpp export_histogram_png/csv, export_linecut_png/csv, export_stats_csv; drivers in src/ui/main_window.cpp do_png/do_csv`
- **Image and diff save** — Save Image A/B or the computed diff to PNG/BMP/PPM (8/10/12/16-bit) preserving HDR/high-bit precision.
  - `src/image_save.cpp perform_save / save_png_impl / save_bmp_impl / save_ppm_impl; UI in src/ui/main_window.cpp render_image_save_window`
- **Clipboard copy** — Two-stage Ctrl/Cmd+C then 1/2/3 copies Image A/B/Diff to the system clipboard as PNG.
  - `src/clipboard_image.cpp clipboard_copy_image; trigger in src/app.cpp handle_keyboard copy-mode (lines 178-196)`
- **Multi-format image loading** — Decode PNG/JPEG/BMP/TGA/HDR plus in-house PPM/PGM (P2/P3/P5/P6, 8/16-bit) into a uniform ImageEntry.
  - `src/image_loader.cpp load_image / try_load_ppm_ascii / try_load_ppm_binary`
- **Directory sequence navigation** — Auto-scan the image's directory and browse next/prev images per panel (';' / 'a', Alt+Wheel, N/Shift+N) with wrap-around and filename toast.
  - `src/image_loader.cpp scan_image_directory / load_image_and_populate_sequence; src/app.cpp sequence_navigate`
- **Slideshow autoplay** — Auto-advance the active panel's sequence on a configurable interval (Shift+A, Shift+Up/Down).
  - `src/main.cpp event-loop slideshow tick (~lines 387-492); src/app.cpp sequence_navigate`
- **Pathfinder minimap** — Bottom-left minimap (thumbnail or schematic) on panel 0 with a viewport-rectangle indicator (P / Ctrl+P).
  - `src/ui/image_panel.cpp ImagePanel::render_pathfinder (line 1716)`
- **Channel isolation** — Isolate R/G/B channels in display and diff (Shift+R/G/B, Shift+C reset).
  - `src/app.cpp handle_keyboard channel cases; applied in shaders and src/ui/image_panel.cpp apply_channel_grid`
- **Image rotation** — Rotate the loaded image 90 degrees CW/CCW in all pixel buffers and re-upload the texture (R / Ctrl+R).
  - `src/image_loader.cpp rotate_image_cw / rotate_image_ccw`
- **Drag-to-zoom and double-click zoom** — Right-drag selects a region that snaps to the best power-of-2 zoom; double-click zooms centered on the cursor.
  - `src/ui/image_panel.cpp handle_mouse_right_select / handle_mouse_double_click`
- **Software rendering fallback** — Full CPU rasterization path (SDL renderer) selected at runtime for GPU-less, VM, or SSH-X11 environments.
  - `src/render_backend.cpp is_software_mode; src/ui/image_panel.cpp cpu_render_image/diff/overlay; src/main.cpp backend selection`
- **CLI configuration** — Launch-time options: --diff-mode, --zoom, --amplify, --geometry, --sync, --fullscreen, --software, --windowed, -bc border colors, -p pan step.
  - `src/app.cpp parse_cli / apply_cli_options`
- **Settings persistence** — Persist UI toggles (border, pixel_format, channel_mode, magnifier) to ~/.av.ini and ImGui layout to av_imgui.ini.
  - `src/app.cpp load_app_ini / save_app_ini`
- **Native open/save dialogs** — Async native file dialogs for opening A/B images and saving, with an in-app 'Open Images' window.
  - `src/image_open.cpp open_open_file_dialog / render_open_images_window; src/image_save.cpp open_context_save_dialog`
- **Menu bar, status bar, hotkey help** — File/View/Diff menus, a status bar summarizing zoom/dims/diff/SSIM/PSNR/indicators, and a hotkey reference window (Ctrl+Shift+H).
  - `src/ui/main_window.cpp render_menubar / render_hotkey_help_window; src/ui/statusbar.cpp StatusBar::render`
- **Windowed/border/fullscreen toggles** — Toggle titlebar windowed mode (W), panel borders (B), fullscreen, and UI visibility (U).
  - `src/app.cpp handle_keyboard SDL_SCANCODE_W / B / U cases`
- **Standalone X11 viewer (av-x11)** — Dependency-light C99 X11 viewer for legacy systems: load 1-2 images, pan/zoom, sync, side-by-side, pixel inspector.
  - `src/av_x11.c main / av_event_loop / av_render_panel`


---

## 4. Findings (버그·기술부채) — high 8 / medium 16 / low 19


### HIGH

- **Screen<->image coordinate transform hand-inlined in ~15 sites (single largest fragility source)**
  - 위치: `src/ui/image_panel.cpp (~15 functions: cpu_render_image:140, cpu_render_diff:332, cpu_render_overlay:677, count_nonzero_diff_pixels:591, apply_threshold_to_diff:631, handle_mouse_double_click:1034, handle_mouse_right_select:1140, render_crosshair:1254, render_magnifier:1436, render_pixel_values:1921, render_diff_pixel_values:2201, render_pathfinder:1765, handle_roi_drag:2464, render_roi_overlay:2503, update_mouse_constraint:1387); also re-derived in src/ui/main_window.cpp pixel balloon:1591-1739 and src/ui/statusbar.cpp:29`
  - 문제: The transform img_px = (screen_px - half_view)/zoom - pan + half_img and its inverse is copy-pasted across roughly a dozen image_panel functions plus the main_window balloon and statusbar. Any change to the convention (e.g. floor vs truncation, a 4th panel, sub-pixel handling) must be edited everywhere; the (int) vs std::floor inconsistency already causes a one-pixel disagreement at top/left edges (sample_pixel:270 uses (int), crosshair/magnifier use floor). This is the highest-leverage source of latent bugs in the codebase.
  - 제안: Add two free functions screen_to_image(vp, half_vw, half_vh, sx, sy) and image_to_screen(...) in viewport.h/.cpp (pure, no ImGui/SDL deps), standardize on std::floor for the pixel snap, and replace all inlined copies. Do it incrementally, one call site per commit, diffing render output to confirm no behavior change.
- **SSIM worker-thread result published to main thread without synchronization (data race)**
  - 위치: `src/diff_engine.cpp SSIMComputer::compute callback (lines 320-330) writing through std::function set in src/ui/main_window.cpp:1345-1348; read at main_window.cpp:1287-1334`
  - 문제: The jthread callback runs cb(std::move(result)) on the worker thread (diff_engine.cpp:328). The registered callback writes the plain global SSIMResult s_ssim_result and sets a plain bool s_ssim_ready=true (main_window.cpp:1347-1348), both read unsynchronized on the main thread (1287). Confirmed in source: s_ssim_ready/s_ssim_result are non-atomic globals (main_window.cpp:587-588). The heatmap vector move and the flag are not ordered, so the main thread can observe ready=true before the result is fully published, or read a torn vector. Real UB.
  - 제안: Make s_ssim_ready a std::atomic<bool> and order writes as: store result, then s_ssim_ready.store(true, release); on the reader use s_ssim_ready.load(acquire) before touching s_ssim_result. Or guard both with a small std::mutex. Minimal change, no API churn.
- **GPU and CPU (software) render paths are near-complete copies; logic already drifting**
  - 위치: `src/ui/image_panel.cpp render_single:1799 vs render_single_software:183; render_diff:2040 vs render_diff_software:711; render_overlay:2539 vs render_overlay_software:826 ('Identical' overlay copy-pasted at 792-815 & 2150-2173; curtain-drag at 858-873 & 2609-2627)`
  - 문제: Each GPU renderer has a CPU twin duplicating size calc, fit/clamp, the input-handler post-pass and overlay drawing. Confirmed drift: render_diff_software:744 calls cpu_render_diff without forwarding state.diff.alpha (cpu_render_diff's alpha defaults to 0.5f), so software AlphaBlend is permanently stuck at 50/50 while the GPU path passes state.diff.alpha at 2084. More divergence is likely as modes are added.
  - 제안: First, the immediate bug: pass state.diff.alpha at render_diff_software:744. Then refactor the shared tail (input handlers + overlay post-pass) into one helper run_panel_postpass(...) called by both GPU and CPU paths so the two only differ in the rasterization step. Extract the duplicated 'Identical' overlay and curtain-drag blocks into named helpers.
- **PPM ASCII export opened in text mode, contradicting its own anti-CRLF comment**
  - 위치: `src/image_save.cpp save_ppm_impl line 85 (verified): FILE* f = std::fopen(path.c_str(), binary ? "wb" : "w");`
  - 문제: Confirmed in source. The comment at line 84 states 'Use rb/wb on all platforms to avoid CRLF issues', but the ternary opens the ASCII P3 path with mode "w", re-introducing CRLF translation on Windows for exactly the format the comment claims to protect. The P6 binary path is correct.
  - 제안: Change the ternary to always use binary mode: std::fopen(path.c_str(), "wb"). P3 ASCII content written with explicit "\n" is valid PPM and avoids platform CRLF translation. One-character-class fix.
- **Failed image load is destructive: prior valid image is freed before the new load is attempted**
  - 위치: `src/image_loader.cpp load_image_and_populate_sequence lines 410-414 (verified)`
  - 문제: Confirmed: free_image(state.images[panel]) runs at line 411 unconditionally when the slot was loaded, then load_image is called at 414. If load_image fails, the panel is left empty (loaded=false), the viewport is reset, PSNR cache invalidated, and the sequence is rebuilt around the failed path (424). The user loses the previously displayed image on any decode error, with only the toast.failed flag signaling failure.
  - 제안: Load into a temporary ImageEntry first; only free the old entry and move-assign on success. On failure, leave state.images[panel], state.views[panel], and the sequence untouched and just set filename_toast.failed. This makes a failed load non-destructive.
- **Diff-mode set is inconsistent across menu, hotkey-help, statusbar label, window title, and CLI**
  - 위치: `src/ui/main_window.cpp render_menubar:730-735, render_hotkey_help_window:927-933, title_diff_label:36-44; src/ui/statusbar.cpp diff_mode_name:6-17; src/app.cpp parse_cli/print_help:53-158; against app.h DiffState::Mode:38-47`
  - 문제: The DiffState::Mode set is described differently in five+ places. The Diff menu exposes only None/Absolute/Relative/Highlight/FalseColor/SSIM (no Enhance, no AlphaBlend) and assigns Ctrl+3..7; the hotkey-help table claims Ctrl+2=AlphaBlend, Ctrl+5=Enhance, Ctrl+9=Highlight; title_diff_label returns nullptr for Enhance/AlphaBlend (no title tag); statusbar diff_mode_name has no AlphaBlend case (renders '?'); and parse_cli/print_help omit AlphaBlend entirely so CLI cannot select it. Users cannot reach Enhance/AlphaBlend from the menu and the documented shortcuts contradict the code.
  - 제안: Create a single source of truth: a constexpr array/table mapping DiffState::Mode -> {display name, hotkey label, title tag, CLI token}. Drive the menu, hotkey-help, statusbar name, title label and CLI parse/help from that one table. As an immediate fix, add the missing Enhance/AlphaBlend entries to the menu, statusbar, title label and CLI so the five locations agree.
- **32-bit int overflow in X11 framebuffer size for large windows**
  - 위치: `src/av_x11.c:156 av_resize_framebuf and :681 main init sizing`
  - 문제: s->win_w * s->win_h * s->bpp is computed in int. A maximized window beyond ~2896px square at 4bpp overflows 32-bit int, yielding a too-small or negative calloc size and subsequent out-of-bounds writes in av_render_panel row-pointer math. On modern high-resolution displays this is reachable, not just theoretical.
  - 제안: Compute the framebuffer size as size_t (e.g. (size_t)win_w * win_h * bpp) at every allocation/index site in av_x11.c, and validate the calloc return. Low-risk, localized to the standalone C viewer.
- **On-screen chart windows re-implement histogram/line-cut logic instead of using the chart_export extractors (already diverging)**
  - 위치: `src/ui/chart_windows.cpp render_histogram_window:256-335, render_hline_cut_window:600-684, render_vline_cut_window:851-936 vs src/chart_export.cpp extract_histogram/extract_*line_cut`
  - 문제: The ImGui chart windows re-bin histograms, redo HDR-max normalization, and re-extract rows/columns inline rather than calling the chart_export extract_* functions used by the PNG/CSV export path. The two implementations already diverge: the on-screen diff histogram gates HDR on (imgA.is_hdr && imgB.is_hdr && f32 non-empty) (chart_windows.cpp:300) while extract_diff_histogram and the diff line-cuts only check f32 non-empty, so the same image can show different distributions on screen versus in the exported file.
  - 제안: Have the ImGui windows call the chart_export extract_* functions and render the returned Data structs, deleting the inline re-implementations. This removes the dual code path and forces on-screen and exported charts to match. Pick one HDR-detection rule (prefer the pixels_f32-non-empty check used by export) and apply it in the shared extractor.

### MEDIUM

- **f32/HDR SSIM uses constants tuned for [0,255] and can exceed range, making scores/heatmap unreliable for HDR**
  - 위치: `src/diff_engine.cpp luma_f32/window_stats_f32 (lines 139-142, 171-192) used by compute_ssim_cpu:261`
  - 문제: The f32 SSIM path multiplies luminance by 255 and reuses SSIM_C1/C2 stabilization constants tuned for an 8-bit [0,255] range. HDR pixels_f32 can exceed 1.0, so luma_f32 can far exceed 255, making C1/C2 negligible and the SSIM score and dissimilarity heatmap meaningless for HDR content. The u8 and f32 paths are not equivalent for out-of-range data.
  - 제안: Derive the dynamic range L from the actual data (e.g. max luminance across both images, or a documented HDR reference) and scale C1=(0.01*L)^2, C2=(0.03*L)^2 accordingly, rather than hardcoding 255. Document the assumption in diff_engine.h. Verify against a known SSIM reference for an HDR pair.
- **Three near-duplicate save-dialog windows (~300 lines) with subtle divergences**
  - 위치: `src/ui/main_window.cpp render_image_save_window:70-242, render_chart_save_window:246-453, render_stats_save_window:457-575`
  - 문제: Lazy path init from dir/stem, per-target loaded-state logic, the disabled-block pattern, colored status text, 'Save All Checked', and the right-aligned Close button are copy-pasted three times with divergences (only image_save resets initialized on close in all branches; status-error coloring duplicated 4x). Related duplication: the PNG/BMP/PPM format-dispatch switch is repeated verbatim in image_save.cpp perform_save's A/B branch and Diff branch (lines 231-243 and 266-278).
  - 제안: Extract a shared render_save_dialog(state, dialog_config) helper parameterized by targets/formats for the three windows, and a save_entry(path, img, sd) helper for the perform_save format dispatch. This removes ~300 lines and the divergence risk.
- **MainWindow::render mixes ImGui rendering with frame-controller side effects**
  - 위치: `src/ui/main_window.cpp render() lines 1251-1376`
  - 문제: render() drains pending open/save requests, deletes/creates GL+SDL textures, kicks off async SSIM, uploads the SSIM heatmap and computes PSNR inside the ImGui render pass. This makes MainWindow::render the de-facto frame controller and couples it to render_backend, SDL and diff_engine threading; it also forces presentation-only code to deal with texture lifetime and worker threads.
  - 제안: Introduce a MainWindow::update(state) (or a free controller step) called between event drain and ImGui::NewFrame that performs the pending-request draining, texture upload/delete, SSIM kickoff and PSNR compute. Leave render() to read state and emit ImGui only. Matches the existing 'do real work at start of next frame' lesson already documented in tasks/lessons.md.
- **compute_diff_pixel_list cache keyed only on image dimensions; stale on same-size content change**
  - 위치: `src/ui/image_panel.cpp compute_diff_pixel_list cache key lines 454-456 (cache_img_a/b_w/h)`
  - 문제: The cached diff listing, 'identical' flags and enhance range are validated only against the four image dimensions. Sequence navigation to a same-size frame, or an in-place reload, keeps the dimensions identical so the cache is never recomputed and shows stale data. The scatter-plot window cache (chart_windows.cpp:1262-1277) has the identical defect, keyed only on width/height.
  - 제안: Add a monotonically-incrementing content_version (or load generation counter) to ImageEntry, bumped in load_image_and_populate_sequence and rotate_*; include both panels' versions in the cache key for compute_diff_pixel_list and the scatter cache. Cheap and eliminates the stale-cache class entirely.
- **DiffRenderer mutates filter state on textures it does not own, silently disabling mipmaps**
  - 위치: `src/diff_engine.cpp render() filter setup lines 63-78`
  - 문제: render() sets MIN/MAG filters to NEAREST (zoom>=1) or LINEAR every frame on the shared image textures, never restoring the GL_LINEAR_MIPMAP_LINEAR set at upload (gl_texture.cpp). It leaves MIN_FILTER without mipmapping, silently disabling the mipmaps gl_upload_texture generated, and the mutation persists for any other render path that later samples the same texture id (single-image, overlay).
  - 제안: Either restore the upload-time filters after the diff draw, or (cleaner) move all sampling-filter decisions into the shader/sampler objects rather than mutating texture parameters, or own a sampler object per panel. Document that texture filter state is render-path-local. Confirm single-image rendering still gets mipmapped minification after the change.
- **DiffRenderer re-binds attribute locations and re-links after compile() already linked, relying on driver leniency**
  - 위치: `src/diff_engine.cpp DiffRenderer::init lines 26-35`
  - 문제: After ShaderProgram::compile() links and deletes the shader stages, init() calls glBindAttribLocation(a_pos=0, a_uv=1) and re-links a program whose shaders were already detached/deleted. This relies on the driver retaining attached shaders across a second link and duplicates link logic compile() already performed. ScreenQuad already assumes locations 0/1, so the explicit binding is effectively redundant when the linker assigns in declaration order; the shaders use 'in' without explicit layout, so it works only by convention.
  - 제안: Add explicit 'layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv;' to VERTEX_SRC (GLSL 150 supports explicit attrib locations via the extension, or bump to 330 which the project already targets) and delete the bind+relink dance. Then compile() is the single link point.
- **LRU ImageCache fully implemented but never consulted (dead abstraction; sequence nav re-decodes every frame)**
  - 위치: `src/image_loader.cpp ImageCache::get:600-624 and global g_image_cache, vs load_image_and_populate_sequence:414 calling load_image directly`
  - 문제: The path-keyed LRU ImageCache (MAX_ENTRIES=8) and its global instance are maintained but the active load funnel calls load_image directly into state.images[panel] and never touches the cache. Sequence next/prev therefore re-decodes from disk every time with zero caching benefit, and the cache code is misleading dead weight that future readers may assume is active.
  - 제안: Decide one way: either wire load_image_and_populate_sequence (or a prefetcher) through g_image_cache.get() to gain the intended caching, or delete ImageCache and g_image_cache. Given sequence navigation is a shipped feature, wiring it up is the higher-value option; if deferring, remove the dead code to avoid the false abstraction.
- **Two PPM parsers duplicate ~90% of their bodies**
  - 위치: `src/image_loader.cpp try_load_ppm_ascii:87-189 and try_load_ppm_binary:193-307`
  - 문제: Header validation, pixels_orig (u16) construction, RGBA8 build, pixels_f32 build, texture upload and error paths are duplicated between the ASCII and binary PPM loaders. Any fix (e.g. a maxval guard, the integer-downscale rounding) must be applied twice. The 8-bit downscale also uses unrounded integer (r*255/maxval), biasing display values low versus the rounded f32 buffer (lines 151-153, 269-271).
  - 제안: Extract the shared post-header tail into finalize_ppm(entry, w, h, maxval, raw_samples) that builds all three buffers (with rounded downscale: (r*255 + maxval/2)/maxval) and uploads the texture; keep only the magic/parse differences in each loader. Aligns display and precise buffers and halves the maintenance surface.
- **Magnifier re-derives diff colors as a 4th copy of the diff math, with int/float and missing-mode divergences**
  - 위치: `src/ui/image_panel.cpp render_magnifier diff color block lines 1521-1557`
  - 문제: The magnifier independently recomputes per-mode diff colors (after cpu_render_diff, the GLSL shader, and the pixel-value text path). Its Enhance remap uses int math (1545-1550) while cpu_render_diff uses a float remap (397-409), so they disagree at boundaries; PixelRelative and AlphaBlend are not handled in the magnifier branch, so the loupe shows PixelAbsolute-style colors for those modes. This is part of the broader per-mode diff logic being duplicated in 3-4 places.
  - 제안: Extract a single diff_color(mode, a_rgba, b_rgba, params) -> rgba CPU function and call it from cpu_render_diff, render_magnifier, and the pixel-value text path; have the GLSL shader mirror the same formula. At minimum, switch the magnifier to the float Enhance remap and add the missing PixelRelative/AlphaBlend cases.
- **Right-click context menu silently fails in ROI and Curtain modes; disabled on overlay panel**
  - 위치: `src/ui/image_panel.cpp handle_mouse_right_select:1089-1091, render_context_popup:1064, and the mode short-circuits at 769-777, 880-885`
  - 문제: render_context_popup is invoked (and the popup opened) only from handle_mouse_right_select, which is skipped in ROI mode and Curtain overlay mode. A context popup opened earlier is not re-rendered in those modes, so the right-click Save As / Copy menu does not work there. Overlay-blend calls right-select with default context_type=-1 (884, 2640), which disables the context menu on the overlay panel entirely.
  - 제안: Always call render_context_popup once per frame for context_type>=0 regardless of ROI/Curtain mode (move it out of the gated handler), and pass a real context_type (0/1/2) from the overlay path. Verify the menu opens and dispatches in ROI, Curtain, and overlay modes.
- **X11 viewer assumes 32-bit TrueColor Visual; no depth check**
  - 위치: `src/av_x11.c:164-168 av_resize_framebuf, shifts derived at :114-117`
  - 문제: XCreateImage uses the default Visual depth but hardcodes bytes_per_line/bpp=4 and bitmap_pad=32, and the framebuffer is packed with 32-bit writes plus alpha forced into bits 24-31. On a 16-bit or non-32bpp Visual this misrenders or crashes; there is no depth validation after deriving r/g/b shifts.
  - 제안: After deriving the Visual in av_init_display, check DefaultDepth/Visual class; if not 24/32-bit TrueColor, either select a matching Visual or print a clear error and exit rather than producing garbage. Low-risk, contained to the fallback binary.
- **X11 viewer: hjkl keyboard pan is a no-op while fit mode is active**
  - 위치: `src/av_x11.c:472-564 KeyPress handler vs MotionNotify drag at :609`
  - 문제: The XK_h/j/k/l pan keys mutate pan but do not clear vp->fit, whereas drag-pan does. After pressing f (fit), keyboard panning leaves fit=1, so the next render re-runs av_fit_viewport and discards the pan, making keyboard pan appear dead in fit mode. Inconsistent with mouse drag.
  - 제안: Set vp->fit = 0 in the hjkl (and arrow) pan key cases, mirroring the drag-pan handler. One line per case, or factor a small begin_pan(vp) helper.
- **Highlight+threshold software diff does 2-3 full per-pixel passes every frame with no caching**
  - 위치: `src/ui/image_panel.cpp render_diff_software:746-760 vs render_diff:2087-2094 (count_nonzero_diff_pixels:591, apply_threshold_to_diff:631)`
  - 문제: In Highlight mode with a threshold, the code iterates the entire viewport calling sample_pixel twice per pixel for the count and again for the threshold, on top of the full cpu_render_diff pass in software mode, so roughly 2-3 full per-pixel passes occur every frame with no caching keyed on viewport/zoom/pan state.
  - 제안: Compute the threshold/exceed counts inside the single cpu_render_diff pass (accumulate while rasterizing) and cache the result keyed on (viewport, zoom, pan, content_version), recomputing only when those change. Eliminates the extra full-image passes.
- **Inconsistent keyboard-capture gating across the input layer**
  - 위치: `src/main.cpp:406 (io.WantCaptureKeyboard), src/app.cpp:525 copy-mode (io.WantTextInput), src/app.cpp:178-196 copy-mode digit interception (no check)`
  - 문제: Three different gating strategies coexist: the main loop gate uses WantCaptureKeyboard, copy-mode entry uses WantTextInput, and the copy-mode digit interception at the top of handle_keyboard runs with no capture check. The behavior of digit keys thus depends on which widget is focused. Intentional per comments but subtle and easy to break when a new text field is added.
  - 제안: Document the intended policy in one place and centralize the decision: a single should_handle_key(io, scancode, mods) helper that the main loop and handle_keyboard both consult, encoding the 'digits intercepted only in copy-mode, global shortcuts need ctrl/gui or !WantCaptureKeyboard' rule explicitly.
- **Q quits unconditionally with no modifier or confirmation**
  - 위치: `src/app.cpp handle_keyboard() case SDL_SCANCODE_Q lines 206-208`
  - 문제: Q triggers an immediate, unconfirmed quit with no modifier requirement. It is mostly shielded today by the main.cpp WantCaptureKeyboard gate, but any path that reaches handle_keyboard while a non-text widget has focus, or any relaxation of the gate, will hard-quit on a stray 'q'. Fragile for a destructive, irreversible action.
  - 제안: Require a modifier (e.g. Ctrl+Q / Cmd+Q) for quit, or keep Q but gate it behind io.WantTextInput==false explicitly within the handler so it cannot fire while any input widget is active. Aligns with the centralized gating fix above.
- **Panel-layout width rule (fb_w, /2, /3) encoded independently in three subsystems**
  - 위치: `src/main.cpp viewport-fit block lines 449-463; src/ui/image_panel.cpp pw/ph recompute at 200/737/843/917/1818/2071/2557; src/ui/main_window.cpp layout 1438-1493 and balloon third_w/half_w`
  - 문제: The 'how many panels are visible -> per-panel width' rule (full / half / third based on both-loaded + diff mode) is computed in main.cpp for fit-zoom, in image_panel.cpp for clamp_pan/draw rect, and in main_window.cpp for layout and balloon hit-testing. A layout change (e.g. a 4th panel) requires synchronized edits in all three, and any drift breaks fit-zoom vs draw-rect vs balloon hit-testing alignment.
  - 제안: Add one function panel_layout(state, fb_w, fb_h) -> array of per-panel rects (origin+size) used by main.cpp fit, main_window.cpp layout, image_panel.cpp clamp/draw, and the balloon. Single source of truth for the split geometry.

### LOW

- **Mutually-exclusive analysis windows enforced by hand-maintained clear-lists in each menu handler**
  - 위치: `src/ui/main_window.cpp View menu lines 665-689`
  - 문제: Exclusivity of histogram/hline/vline/stats windows is enforced by each MenuItem handler manually clearing the other three flags — four hand-maintained clear-lists. Adding a fifth analysis window requires editing every existing handler, an error-prone pattern.
  - 제안: Replace the four bools with a single 'active_analysis' enum on AppState; menu items set the enum, and each render_*_window guards on enum==its value. Exclusivity becomes structural and adding a window is one enum value.
- **Two INI persistence files with inconsistent location strategy**
  - 위치: `src/main.cpp:306 io.IniFilename = "av_imgui.ini" (CWD-relative) vs src/app.cpp ini_path() ~/.av.ini (lines 661-665)`
  - 문제: The ImGui layout INI is a relative path written to the current working directory, while the app INI uses $HOME/.av.ini. Launching av from different directories silently fragments the ImGui layout persistence.
  - 제안: Point io.IniFilename at an absolute path under $HOME (e.g. ~/.av_imgui.ini) using the same home-resolution logic as ini_path(), so both files live in a stable, single location.
- **Edge-pan keys write a 1e6f sentinel relying on clamp in another subsystem**
  - 위치: `src/app.cpp pan-to-edge keys: H gui+shift sets pan_x=1e6f at lines 346-349; J/K/L analogues 366-411`
  - 문제: Edge-panning writes a 1e6f sentinel into pan_x/pan_y and depends on viewport_clamp_pan (implemented in image_panel.cpp) to pull it back. This couples the keyboard layer to clamp behavior in another subsystem; any render path that does not clamp lets the 1e6f offset escape and the image disappears off-screen.
  - 제안: Add explicit viewport_pan_to_edge(vp, dir) helpers in viewport.cpp that compute the clamped edge offset directly from image/view size, and call those from the keyboard handler instead of writing a sentinel. Keeps pan math self-contained in viewport.*.
- **SDL software-texture upload relies on undocumented assumption that SDL does not retain surface pixels**
  - 위치: `src/image_loader.cpp upload_sdl_texture line 58`
  - 문제: SDL_CreateSurfaceFrom is handed a const_cast'd pointer into the caller's pixels buffer; it is safe today because SDL_CreateTextureFromSurface copies before the surface is destroyed, but the const_cast plus the no-retain assumption is undocumented and would break silently if the upload path changed to a streaming/zero-copy surface.
  - 제안: Add a brief comment stating the contract (SDL copies pixels into the texture before the surface is freed; surface must outlive the CreateTextureFromSurface call) at the call site, so a future refactor does not introduce a use-after-free.
- **Two near-identical SDL dialog callbacks with asymmetric memory ownership**
  - 위치: `src/image_open.cpp open_dialog_callback:18-29 (heap userdata, delete) vs src/image_save.cpp context_save_callback:294-300 (pointer into persistent state, no alloc)`
  - 문제: The open callback heap-allocates OpenDialogUserData and deletes it; the save callback uses a pointer into persistent AppState with no allocation. The asymmetry is undocumented and easy to get wrong when adding a third dialog. Source-selection logic (swap_images + pixels/pixels_f32/diff dispatch) is also duplicated between clipboard_copy_image and image_save.cpp:254.
  - 제안: Document the two ownership patterns and standardize new dialogs on one (prefer the no-alloc persistent-state pattern). Separately, factor target_type -> (rgba_data, w, h) resolution into one shared helper in image_save used by both clipboard_copy_image and perform_save.
- **falsecolor ramp and pixel-grid/viewport mapping duplicated across 3-4 shaders plus a C++ copy**
  - 위치: `src/shader_sources.h falsecolor() in DIFF_FRAG_SRC:94-102 & SSIM_FRAG_SRC:213-221; >=16x grid block in IMAGE/DIFF/BLEND frag; the same ramp re-implemented in C++ at src/ui/main_window.cpp:1307-1310`
  - 문제: The false-color colormap, the pixel-grid overlay, and the screen->img viewport mapping are copy-pasted across all fragment shaders, with no shared GLSL include, and the colormap is additionally re-implemented in C++ for the software SSIM path. Changing the colormap or viewport math requires editing 3-4 GLSL copies plus the C++ copy, a real drift risk across two languages.
  - 제안: Concatenate a shared GLSL prelude string (falsecolor + grid + mapping helpers) into each fragment shader at compile time so there is one GLSL definition, and have the C++ software falsecolor call a single shared C++ function that mirrors it (or generate both from one table). Reduces the copies from 4-5 to 2 (one per language).
- **SoftRenderer blend_pixel forces output alpha to 255, preventing transparent PNG output**
  - 위치: `src/soft_renderer.cpp blend_pixel lines 27-42`
  - 문제: blend_pixel always sets output alpha to 255 even when alpha-blending, so the canvas can never produce a partially-transparent result; any transparent-PNG use of save_png would silently get an opaque image. Fine for the current opaque charts but a latent surprise.
  - 제안: Composite alpha correctly (out_a = src_a + dst_a*(1-src_a)) instead of forcing 255, or document in soft_renderer.h that the canvas is opaque-only and save_png emits opaque RGBA. Pick based on whether transparent export is ever wanted.
- **Sync-mirror logic copy-pasted into ~8 X11 input branches**
  - 위치: `src/av_x11.c sync mirror at :484-485, :491-493 and ~6 more KeyPress/ButtonPress/MotionNotify branches`
  - 문제: The 'if (s->sync && s->num_images==2) s->vp[1-panel]... = ...' mirror is duplicated across roughly eight input branches. Heavy duplication; a branch that forgets to mirror produces an inconsistent sync, and the per-branch clamp ordering is fragile.
  - 제안: Add a single sync_viewports(s, src_panel) helper called once at the end of input handling (after the active viewport is mutated), and remove the inline mirrors. Centralizes the clamp+copy and removes the forget-to-mirror risk.
- **render_diff_listing_window 'Go to' can index an empty vector; half-dims ignore images[1]**
  - 위치: `src/ui/main_window.cpp render_diff_listing_window 'Go to' lines 1173-1189, 'List from' clamp 1160-1162`
  - 문제: filtered_indices is indexed after std::clamp to [1,total]-1; if total==0 the clamp yields index 0 and filtered_indices[0] on an empty vector is unguarded (the earlier identical/computed guard is the only thing preventing entry). Also half_iw/half_ih use images[0] dimensions only, ignoring images[1] when sizes differ.
  - 제안: Add an explicit early return when filtered_indices.empty() before any indexing, and use the relevant image's own dimensions (or min of both) for the half-size math in the diff context.
- **compute_ssim_cpu is O(w*h*121) double precision with coarse cancellation**
  - 위치: `src/diff_engine.cpp compute_ssim_cpu lines 234-289`
  - 문제: SSIM is computed with a non-separable 11x11 window in double precision, re-fetching overlapping windows per pixel, with cancellation checked only once per row (257). For large images it is very slow and a wide image does a full row of work before honoring cancel. It is on a background thread so it does not block the UI, hence low severity, but it is the heaviest CPU cost in the app.
  - 제안: Use the separable Gaussian (two 1D passes) and integral-image/running-window stats to drop from O(N*121) to ~O(N), and check the cancel token every K pixels rather than per row. Only worth doing if SSIM latency on large images is a real complaint.
- **Median is a coarse lower-bin percentile, not interpolated**
  - 위치: `src/chart_export.cpp compute_channel_stats_u8 ~55-60 and compute_channel_stats_f32 ~121-129`
  - 문제: Median is reported as the lower-bin/value where the cumulative count first reaches npix/2, with no interpolation and no averaging of the two middle samples for even npix. For u8 it is biased low; for f32 resolution is limited to 1024 bins over [min,max]. Labeled 'Median' but is effectively a coarse percentile.
  - 제안: Either interpolate within the bin / average the two central samples for even counts, or relabel the metric as an approximate median in the stats table and CSV so it is not mistaken for an exact median.
- **Adding one ChannelStats metric requires four synchronized edit sites**
  - 위치: `src/chart_export.h ChannelStats struct; compute_channel_stats_u8 & _f32 in chart_export.cpp; export_stats_csv; draw_stats_table in chart_windows.cpp`
  - 문제: A new statistic must be added to the struct, computed in both the u8 and f32 kernels (which must stay in sync), added as a CSV row(), and added as a draw_stats_table row_vals() — four+ sites for one metric, with the u8/f32 kernels being the easiest to forget.
  - 제안: Drive the stats table/CSV from a single descriptor list ({name, accessor, description}) so adding a field updates rendering and export automatically; keep only the two kernel computations manual, and consider a shared templated accumulation to reduce u8/f32 divergence.
- **HDR histogram normalization floor differs between absolute (1.0) and diff (1e-6) paths**
  - 위치: `src/chart_export.cpp extract_histogram f32 path ~239 and inline f32 histogram chart_windows.cpp ~259 (floor 1.0) vs extract_diff_histogram (floor 1e-6)`
  - 문제: The absolute HDR histogram clamps max_v to a floor of 1.0, so images whose max channel value is < 1.0 are binned against 1.0 rather than their true max, compressing data into low bins; the diff histogram uses a 1e-6 floor. Behavior is inconsistent between absolute and diff histograms, and the absolute path under-uses the bin range for sub-1.0 HDR images.
  - 제안: Use the true per-image max (with a small 1e-6 epsilon floor only to avoid divide-by-zero) consistently in both absolute and diff histogram normalization, and apply it in the shared extractor once the on-screen windows are switched to the extractors.
- **render_linecut_to_sr discards x_label and mislabels the y=0 axis tick**
  - 위치: `src/chart_export.cpp render_linecut_to_sr ~661, ~706`
  - 문제: x_label is accepted then (void)-discarded, so exported line-cut PNGs never get the 'Pixel Column'/'Pixel Row' X-axis label that callers pass (main_window.cpp:335,339). The '0' y-axis tick is drawn at cy+ch/2 (mid-height) instead of the chart bottom, mislabeling the axis.
  - 제안: Draw the passed x_label under the X axis and position the y=0 tick at the chart bottom (cy+ch). Small, self-contained rendering fixes.
- **Separate-channel export filename split uses rfind('.') on full path**
  - 위치: `src/chart_export.cpp export_histogram_png ~638-651 and export_linecut_png ~756-764`
  - 문제: Filename derivation for {base}_R/_G/_B splits on rfind('.') over the full path; a directory component containing a dot but an extensionless filename (e.g. '/a.b/chart') splits at the wrong place. Edge case, but no basename isolation before the split.
  - 제안: Isolate the basename via path_basename (already available in path_utils.h) before rfind('.'), or split only after the last path separator. One-line guard using the existing helper.
- **PixelRelative denominator clamp in compute_diff_cpu is a no-op**
  - 위치: `src/image_save.cpp compute_diff_cpu lines 181-183`
  - 문제: The relative-diff denominator is std::max(ar + 0.001f, 0.001f); since ar is in [0,1], ar+0.001 is always >= 0.001, so the std::max never clamps. The guard against small denominators is effectively dead and only matches the shader if the shader has the same redundancy.
  - 제안: Clamp on the magnitude that can actually be near zero (e.g. max(ar, eps) as the denominator, or max(|ar|, eps)) so the floor is meaningful; align the GLSL relative-diff denominator to the same expression.
- **Dead/no-op code: render_crosshair sync block, av_x11 bg_pix, viewport_zoom_in branch**
  - 위치: `src/ui/image_panel.cpp render_crosshair:1361-1369 (empty sync block); src/av_x11.c:333 bg_pix computed then (void)-discarded; src/viewport.cpp viewport_zoom_in:31-45 (no-op else-if branch)`
  - 문제: Three unrelated dead-code instances grouped for one cleanup pass: the cross-panel crosshair sync block contains only a comment and does nothing (feature unimplemented); av_x11 bg_pix is computed (252-255) and never used; viewport_zoom_in has an over-complicated branch with a no-op else-if that reduces to 'first level strictly greater than current zoom' and is hard to verify at fractional fit-zooms (zoom_out is the cleaner pattern to mirror).
  - 제안: Remove bg_pix and the empty crosshair sync block; simplify viewport_zoom_in to 'select the first ZOOM_LEVELS entry strictly greater than v.zoom, else stay at max', mirroring viewport_zoom_out. If cross-panel crosshair is wanted, file it as a real TODO rather than leaving an empty block.
- **gl_upload_texture mishandles 2-channel (gray+alpha) buffers**
  - 위치: `src/gl_texture.cpp gl_upload_texture lines 21-31`
  - 문제: channels==2 is not handled and falls through to RGBA8/GL_RGBA defaults, which would misread a 2-channel buffer. Only 1/3/4 channels are handled. Not currently triggered because loaders force 4 channels, but a latent trap for any future 2-channel path.
  - 제안: Add an explicit channels==2 case (GL_RG/GL_RG8) or assert/reject channels not in {1,3,4} so a future caller gets a clear failure rather than silent corruption.
- **main.cpp software-renderer error path leaks the just-created window/renderer**
  - 위치: `src/main.cpp SDL_CreateRenderer failure ~line 282 returning 2 before cleanup.window is assigned at line 292`
  - 문제: On the software path, a SDL_CreateRenderer failure does 'return 2' while cleanup.window has not yet been assigned (assignment is at line 292), so the just-created window and renderer are not tracked by the SdlCleanup RAII and leak on that error path. Other early returns are mostly covered by incremental cleanup flags; this window-before-cleanup.window gap is the real defect. Low impact since it is a fatal-exit path, but it is a correctness smell in the RAII contract.
  - 제안: Assign cleanup.window (and cleanup.renderer) immediately after each SDL resource is created, before any subsequent fallible call, so every early return tears them down. Tightens the RAII invariant.


---

## 5. 개선/신기능 아이디어 (15)

- **[small] Extract a single source-selection helper resolving target_type (A/B/Diff) -> (rgba bytes, w, h)**
  - 근거: The swap_images resolution plus pixels/pixels_f32/diff dispatch is duplicated between clipboard_copy_image and image_save.cpp perform_save, creating divergence risk whenever a new target or source format is added. A shared helper in image_save would let clipboard and file-save consume one path, and is a clean prerequisite for new copy/save targets.
  - 터치: src/image_save.h/.cpp (new resolve_save_source helper, perform_save); src/clipboard_image.cpp clipboard_copy_image
- **[small] Fix software-mode AlphaBlend ignoring the user alpha**
  - 근거: render_diff_software calls cpu_render_diff without passing alpha, so cpu_render_diff's default 0.5f is used and software-mode AlphaBlend is permanently stuck at 50/50 while the GPU path correctly passes state.diff.alpha. This is a concrete behavioral divergence between backends for a shipped feature.
  - 터치: src/ui/image_panel.cpp render_diff_software (pass state.diff.alpha to cpu_render_diff)
- **[small] Add AlphaBlend to --diff-mode CLI parsing and the help text, and reconcile the diff-mode set across menu/help/statusbar/title**
  - 근거: AlphaBlend (Ctrl+2) is a first-class keyboard-reachable mode but cannot be selected via --diff-mode and is omitted from print_help; separately the diff-mode set is inconsistent across the Diff menu, hotkey-help table, statusbar diff_mode_name, and title_diff_label (Enhance/AlphaBlend have no menu entry, title shows no tag, statusbar shows '?'). Reconciling these removes user-visible drift and unreachable modes.
  - 터치: src/app.cpp parse_cli/print_help; src/ui/main_window.cpp render_menubar Diff menu + render_hotkey_help_window + title_diff_label; src/ui/statusbar.cpp diff_mode_name
- **[small] Replace mutually-exclusive analysis-window flags with a single active-analysis enum**
  - 근거: histogram/hline/vline/stats exclusivity is enforced by four hand-maintained clear-lists in each menu handler; adding a fifth analysis panel requires editing all existing handlers. A single AppState enum makes exclusivity automatic and is a clean precondition for new analysis windows.
  - 터치: src/app.h (replace show_histogram/hline/vline/stats bools with an enum); src/ui/main_window.cpp View menu handlers + the floating-windows render block; src/ui/chart_windows.cpp window guards
- **[small] Add a JPEG (and optionally TIFF) save format**
  - 근거: Save currently supports only PNG/BMP/PPM; JPEG output is a common ask for sharing and is trivial to add via stb_image_write, reusing build_rgb_u8. The format list is duplicated between perform_save's A/B and Diff branches and the dialog UI, so doing the perform_save save_entry extraction (separate idea) first makes this a one-line addition.
  - 터치: src/app.h ImageSaveDialog::Format; src/image_save.cpp (save_jpg_impl, perform_save switches, dialog filters); save dialog radio UI in src/ui/main_window.cpp
- **[small] Fix the SSIM result data race with a proper atomic/mutex handoff**
  - 근거: The SSIM worker thread writes s_ssim_result and sets a plain bool s_ssim_ready with no synchronization, so the main thread can observe the flag before the heatmap vector is fully published or read a torn vector. This is a correctness bug in a shipped async feature and should use release/acquire atomics or a mutex.
  - 터치: src/diff_engine.cpp SSIMComputer::compute callback; src/ui/main_window.cpp s_ssim_result/s_ssim_ready statics and their consumption
- **[small] Add a quit confirmation or require a modifier for the destructive Q-quit**
  - 근거: Q quits unconditionally with no modifier or confirmation; it is only shielded by the main-loop keyboard gate, so any relaxation of that gate or a non-text widget focus path hard-quits on a stray keystroke. A modifier requirement or a confirmation prompt makes the destructive action robust, matching staff-engineer expectations for a viewer that may hold unsaved ROI/analysis state.
  - 터치: src/app.cpp handle_keyboard SDL_SCANCODE_Q case; optionally a confirm modal in src/ui/main_window.cpp
- **[medium] Unify the screen<->image coordinate transform into two shared helpers (screen_to_image / image_to_screen)**
  - 근거: The inverse viewport transform is hand-inlined in ~15 functions in image_panel.cpp and re-derived again in main_window.cpp's balloon and statusbar. This is flagged as the single largest source of fragility/bug risk; any convention change must currently be edited in a dozen places and the fit-zoom vs draw-rect panel-width math already risks drift across two subsystems.
  - 터치: src/ui/image_panel.cpp (cpu_render_*, handle_mouse_*, render_crosshair/magnifier/pixel_values/pathfinder/roi, update_mouse_constraint); src/ui/main_window.cpp pixel balloon; src/viewport.h/.cpp (natural home for the helpers)
- **[medium] Replace the three near-duplicate save dialogs and the per-branch format-dispatch switch with one shared helper**
  - 근거: render_image_save_window / render_chart_save_window / render_stats_save_window are ~300 lines of copy-paste with subtle divergences, and perform_save duplicates the PNG/BMP/PPM switch in its A/B and Diff branches. Consolidating removes drift risk and is explicitly called out as removing ~300 lines.
  - 터치: src/ui/main_window.cpp (three render_*_save_window functions); src/image_save.cpp perform_save (extract save_entry helper)
- **[medium] Wire the existing LRU ImageCache into load_image_and_populate_sequence with sequence prefetch**
  - 근거: ImageCache (MAX_ENTRIES=8, global g_image_cache) is fully implemented but dead — the load funnel re-decodes every image from disk on each next/prev, so sequence navigation and slideshow re-decode large files needlessly. Wiring get() into the single funnel adds real caching and enables prefetching the next sequence frame, with no new abstraction to build.
  - 터치: src/image_loader.cpp load_image_and_populate_sequence, ImageCache::get/evict_lru; src/image_loader.h MAX_ENTRIES; optional prefetch hook in src/main.cpp slideshow tick
- **[medium] Add a content/version counter to invalidate the diff-pixel-list and SSIM caches when same-size frames change**
  - 근거: compute_diff_pixel_list keys its cache only on the four image dimensions, so navigating to a same-size frame or reloading in place leaves stale diff listings, identical flags, and enhance ranges. SSIM and scatter caches have the same dimension-only-key bug. A monotonically-incremented version stamp on ImageEntry (bumped in load funnel and rotate) gives a single robust invalidation key.
  - 터치: src/app.h ImageEntry (add version field); src/image_loader.cpp load funnel + rotate_image_* (bump version); src/ui/image_panel.cpp compute_diff_pixel_list cache key; src/ui/main_window.cpp SSIM trigger; src/ui/chart_windows.cpp scatter cache
- **[medium] Make line-cut position user-selectable instead of fixed at canvas center**
  - 근거: Horizontal/vertical cuts are derived purely from ViewportState pan and locked to canvas center; letting the user pick the row/column (e.g. drag a guide or numeric input) makes line-cut analysis far more useful. It requires threading a position through both the extractor and the duplicated inline computation in the windows — a natural follow-on to deduplicating chart extraction.
  - 터치: src/chart_export.cpp extract_hline_cut/extract_vline_cut + diff variants; src/ui/chart_windows.cpp render_hline_cut_window/render_vline_cut_window inline recompute; src/app.h (new cut-position field)
- **[medium] Make the on-screen histogram/line-cut windows call the chart_export extractors instead of re-implementing binning inline**
  - 근거: The ImGui histogram and line-cut windows re-implement binning, HDR normalization, and row/column extraction inline rather than calling extract_* used by the export path, and the two already diverge (e.g. the diff-histogram HDR gate differs between screen and export). Routing both through the extractors guarantees what-you-see-is-what-you-export and removes a documented high-severity duplication.
  - 터치: src/ui/chart_windows.cpp render_histogram_window/render_hline_cut_window/render_vline_cut_window; src/chart_export.cpp extract_histogram/extract_*line_cut (reuse as the single source)
- **[large] Add a new diff mode (e.g. logarithmic or edge diff) using the documented mode-extension pattern**
  - 근거: The codebase has a well-trodden path for new diff modes (enum + DIFF_FRAG_SRC branch + render() int map + CPU branch in cpu_render_diff + compute_diff_cpu for save + magnifier color), and lessons.md documents it as an established pattern. A log/edge diff is genuinely useful for visualizing small render differences and fits cleanly. The main caveat is the mode logic lives in 4+ places, so consolidating diff math first (see coordinate/transform ideas) reduces the touch count.
  - 터치: src/app.h DiffState::Mode; src/shader_sources.h DIFF_FRAG_SRC; src/diff_engine.cpp render() switch; src/ui/image_panel.cpp cpu_render_diff + render_magnifier; src/image_save.cpp compute_diff_cpu; menu/help/statusbar labels
- **[large] Add an additional decodable input format (e.g. TIFF or EXR) via the load try-chain**
  - 근거: load_image is a clean try-chain ending in stb, and the extension-extension pattern is documented; EXR/TIFF would broaden the tool's usefulness for render engineers (the stated audience). The friction is the supported-extension list is duplicated in three places (SUPPORTED_IMG_EXTS, the open-dialog filter, sequence scan), so this idea pairs well with deduplicating that list first.
  - 터치: src/image_loader.cpp load_image (new try_load_<fmt>); src/image_loader.h SUPPORTED_IMG_EXTS; src/image_open.cpp dialog filter string; (optionally a small new third-party decoder)