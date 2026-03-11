# av — Advanced Pixel Lens

A fast, GPU-accelerated image viewer and comparator for engineers and artists who need pixel-accurate inspection.

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![C++](https://img.shields.io/badge/C%2B%2B-20-orange)

---

## Features

### Image Viewing
- Side-by-side A/B image comparison with synchronized pan/zoom
- Zoom range: 0.1× to 32× with smooth interpolation
- Fit-to-window mode and pixel-perfect 1:1 view
- HDR image support (EXR, HDR via stb)
- Channel isolation: R, G, B, or composite RGB
- Pixel value balloon on cursor hover (press `V`)

### Diff & Analysis
- **Pixel Absolute / Relative diff** with adjustable amplification
- **False Color diff** for intuitive error visualization
- **SSIM heatmap** — structural similarity index map
- **Histogram** with per-channel overlay
- **Horizontal / Vertical line cut** plot
- **Image statistics** (mean, std-dev, min/max per channel)
- **ROI (Region of Interest)** — drag to select and compute stats

### Comparison Modes
- **Overlay / Blend** — alpha-composite A and B (`O` key)
- **Curtain mode** — drag a vertical divider between A and B
- **Swap** — instantly swap A↔B (`S` key)

### Navigation
- Image sequence browsing: `[` / `]` to step through files in the same directory
- Vim-style keyboard pan: `hjkl` (fine), `Shift+hjkl` (coarse)
- Drag-to-pan, scroll-to-zoom

### Export
- Save images as PNG, BMP, or PPM (8/10/12/16-bit)
- Export histogram/line-cut charts as PNG or CSV
- Export statistics as CSV

### Scatter Plot
- Per-channel scatter plot comparing pixel values of A vs B

---

## Dependencies

All dependencies are fetched automatically by CMake (no manual installation required):

| Library | Version | Purpose |
|---------|---------|---------|
| [SDL3](https://github.com/libsdl-org/SDL) | 3.2.0 | Window, input, file dialogs |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking branch | GUI |
| [glad2](https://github.com/Dav1dde/glad) | 2.0.6 | OpenGL loader |
| [stb](https://github.com/nothings/stb) | master | Image decode/encode |

**Optional:**
- [lcms2](https://www.littlecms.com/) — ICC color profile support (auto-detected via pkg-config)

**System requirements:**
- CMake ≥ 3.24
- C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- OpenGL 3.3 Core capable GPU

---

## Building

### Linux / WSL

```bash
# Install dependencies (Ubuntu/Debian)
bash script/wsl-setup.sh

# Build (Release)
bash script/build-linux.sh

# Build (Debug)
bash script/build-linux.sh debug

# Clean rebuild
bash script/build-linux.sh clean
```

Binary is placed at `bin/av` and auto-installed to `~/.local/bin/av`.

**Linux AppImage** (portable, no installation required):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target appimage
# → bin/av-x86_64.AppImage  (or aarch64)
```

### macOS (Apple Silicon)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
# → bin/av
```

### Windows (Visual Studio 2022)

```bat
script\build-windows.bat
```

Or manually:
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Binary is placed at `bin\av.exe` and auto-installed to `%LOCALAPPDATA%\av\`.

---

## Usage

```
av [options] [image_a] [image_b]

Options:
  --diff <mode>     Start in diff mode: abs, rel, false, ssim
  --zoom <factor>   Initial zoom (default: fit)
  --amplify <n>     Diff amplification factor (default: 1.0)
  --fullscreen      Start in fullscreen
  --width <n>       Window width (default: 1280)
  --height <n>      Window height (default: 720)
  --no-sync         Disable viewport sync
  --icc <file>      ICC profile path for color management
  --no-color-mgmt   Disable ICC color management
  --pan-step <n>    Shift+hjkl step in pixels (default: 32)
  --help            Show this help
  --version         Show version
```

**Examples:**
```bash
av image_a.png image_b.png
av --diff abs --amplify 5 before.png after.png
av reference.exr render.exr --diff ssim
```

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `O` | Open file dialog |
| `S` | Swap A ↔ B |
| `U` | Toggle UI overlay |
| `V` | Toggle pixel value balloon |
| `H` | Toggle histogram |
| `Ctrl+L` | Horizontal line cut |
| `Ctrl+Y` | Vertical line cut |
| `Ctrl+S` | Image statistics |
| `Ctrl+E` | ROI select mode |
| `O` | Overlay / Blend mode |
| `[` / `]` | Previous / Next image in directory |
| `hjkl` | Pan (fine) |
| `Shift+hjkl` | Pan (coarse) |
| `+` / `-` | Zoom in / out |
| `F` | Fit to window |
| `1` | 1:1 pixel view |
| `D` | Cycle diff mode |
| `Tab` | Switch active panel |
| `Ctrl+Shift+H` | Hotkey reference |
| `Q` / `Esc` | Quit |

---

## License

MIT License — see [LICENSE](LICENSE).
