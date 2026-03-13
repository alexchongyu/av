#include "image_save.h"
#include "path_utils.h"

#include <stb_image_write.h>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Get a pixel from an ImageEntry as linear float RGB [0, 1].
// For HDR images uses pixels_f32; for LDR images normalises from pixels (u8).
static void get_pixel_f32(const ImageEntry& img, int ix, int iy,
                           float& r, float& g, float& b)
{
    int pidx = (iy * img.width + ix) * 4;
    if (img.is_hdr && !img.pixels_f32.empty()) {
        r = img.pixels_f32[pidx + 0];
        g = img.pixels_f32[pidx + 1];
        b = img.pixels_f32[pidx + 2];
    } else if (!img.pixels.empty()) {
        r = img.pixels[pidx + 0] / 255.0f;
        g = img.pixels[pidx + 1] / 255.0f;
        b = img.pixels[pidx + 2] / 255.0f;
    } else {
        r = g = b = 0.0f;
    }
}

static inline float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// Build an RGB u8 buffer (3 bytes/pixel, no alpha) from an ImageEntry.
// HDR f32 values are clamped to [0, 1] before scaling.
static std::vector<uint8_t> build_rgb_u8(const ImageEntry& img)
{
    std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            float fr, fg, fb;
            get_pixel_f32(img, x, y, fr, fg, fb);
            size_t out = (static_cast<size_t>(y) * img.width + x) * 3;
            rgb[out + 0] = static_cast<uint8_t>(std::round(clamp01(fr) * 255.0f));
            rgb[out + 1] = static_cast<uint8_t>(std::round(clamp01(fg) * 255.0f));
            rgb[out + 2] = static_cast<uint8_t>(std::round(clamp01(fb) * 255.0f));
        }
    }
    return rgb;
}

// ─── PNG / BMP savers ─────────────────────────────────────────────────────────

static bool save_png_impl(const std::string& path, const ImageEntry& img)
{
    auto rgb = build_rgb_u8(img);
    return stbi_write_png(path.c_str(), img.width, img.height, 3,
                          rgb.data(), img.width * 3) != 0;
}

static bool save_bmp_impl(const std::string& path, const ImageEntry& img)
{
    auto rgb = build_rgb_u8(img);
    return stbi_write_bmp(path.c_str(), img.width, img.height, 3, rgb.data()) != 0;
}

// ─── PPM saver ────────────────────────────────────────────────────────────────
// Handles both u8 and f32 sources, any supported bit depth.
// binary=true → P6, binary=false → P3
// bits: 8, 10, 12, 16

static bool save_ppm_impl(const std::string& path, const ImageEntry& img,
                          bool binary, int bits)
{
    int maxval = (1 << bits) - 1;
    bool multi_byte = (bits > 8);

    // Use "rb"/"wb" on all platforms to avoid CRLF issues
    FILE* f = std::fopen(path.c_str(), binary ? "wb" : "w");
    if (!f) return false;

    // Write header
    std::fprintf(f, "%s\n%d %d\n%d\n",
                 binary ? "P6" : "P3",
                 img.width, img.height, maxval);

    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            float fr, fg, fb;
            get_pixel_f32(img, x, y, fr, fg, fb);

            int ir = static_cast<int>(std::round(clamp01(fr) * static_cast<float>(maxval)));
            int ig = static_cast<int>(std::round(clamp01(fg) * static_cast<float>(maxval)));
            int ib = static_cast<int>(std::round(clamp01(fb) * static_cast<float>(maxval)));

            if (binary) {
                if (multi_byte) {
                    uint8_t buf[6];
                    buf[0] = static_cast<uint8_t>((ir >> 8) & 0xFF);
                    buf[1] = static_cast<uint8_t>( ir       & 0xFF);
                    buf[2] = static_cast<uint8_t>((ig >> 8) & 0xFF);
                    buf[3] = static_cast<uint8_t>( ig       & 0xFF);
                    buf[4] = static_cast<uint8_t>((ib >> 8) & 0xFF);
                    buf[5] = static_cast<uint8_t>( ib       & 0xFF);
                    std::fwrite(buf, 1, 6, f);
                } else {
                    uint8_t buf[3] = {
                        static_cast<uint8_t>(ir),
                        static_cast<uint8_t>(ig),
                        static_cast<uint8_t>(ib)
                    };
                    std::fwrite(buf, 1, 3, f);
                }
            } else {
                std::fprintf(f, "%d %d %d\n", ir, ig, ib);
            }
        }
    }

    std::fclose(f);
    return true;
}

// ─── Diff computation (CPU mirror of diff fragment shader) ────────────────────

// Mirrors the falsecolor() function in shader_sources.h
static void falsecolor_cpu(float t, float& r, float& g, float& b)
{
    t = clamp01(t);
    if (t < 0.25f) {
        float f = t * 4.0f;
        r = 0.0f; g = 0.0f; b = 0.5f + 0.5f * f;
    } else if (t < 0.5f) {
        float f = (t - 0.25f) * 4.0f;
        r = 0.0f; g = f; b = 1.0f - f;
    } else if (t < 0.75f) {
        float f = (t - 0.5f) * 4.0f;
        r = f; g = 1.0f; b = 0.0f;
    } else {
        float f = (t - 0.75f) * 4.0f;
        r = 1.0f; g = 1.0f - f; b = 0.0f;
    }
}

// Returns RGBA8 diff image (pixels[0..3] = R,G,B,A per pixel).
// SSIM falls back to PixelAbsolute.
std::vector<uint8_t> compute_diff_cpu(const ImageEntry& imgA,
                                       const ImageEntry& imgB,
                                       const DiffState&  diff)
{
    int w = std::min(imgA.width,  imgB.width);
    int h = std::min(imgA.height, imgB.height);

    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);

    DiffState::Mode mode = diff.mode;
    if (mode == DiffState::Mode::SSIM)
        mode = DiffState::Mode::PixelAbsolute;   // SSIM fallback

    float amp = diff.amplify;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float ar, ag, ab, br, bg, bb;
            get_pixel_f32(imgA, x, y, ar, ag, ab);
            get_pixel_f32(imgB, x, y, br, bg, bb);

            float dr = std::fabs(ar - br);
            float dg = std::fabs(ag - bg);
            float db = std::fabs(ab - bb);

            float or_, og, ob;

            if (mode == DiffState::Mode::PixelRelative) {
                dr = dr / std::max(ar + 0.001f, 0.001f);
                dg = dg / std::max(ag + 0.001f, 0.001f);
                db = db / std::max(ab + 0.001f, 0.001f);
                or_ = clamp01(dr * amp);
                og  = clamp01(dg * amp);
                ob  = clamp01(db * amp);
            } else if (mode == DiffState::Mode::FalseColor) {
                float intensity = (dr + dg + db) / 3.0f;
                falsecolor_cpu(intensity * amp, or_, og, ob);
            } else {
                // PixelAbsolute (default)
                or_ = clamp01(dr * amp);
                og  = clamp01(dg * amp);
                ob  = clamp01(db * amp);
            }

            size_t idx = (static_cast<size_t>(y) * w + x) * 4;
            out[idx + 0] = static_cast<uint8_t>(std::round(or_ * 255.0f));
            out[idx + 1] = static_cast<uint8_t>(std::round(og  * 255.0f));
            out[idx + 2] = static_cast<uint8_t>(std::round(ob  * 255.0f));
            out[idx + 3] = 255;
        }
    }
    return out;
}

// ─── perform_save ─────────────────────────────────────────────────────────────

void perform_save(const std::string& path,
                  ImageSaveDialog::Target target,
                  AppState& state)
{
    auto& sd = state.image_save;
    sd.status_error = false;
    sd.status_msg.clear();

    bool ok = false;

    if (target == ImageSaveDialog::Target::ImageA ||
        target == ImageSaveDialog::Target::ImageB)
    {
        int idx = (target == ImageSaveDialog::Target::ImageA) ? 0 : 1;
        const ImageEntry& img = state.images[idx];

        if (!img.loaded) {
            sd.status_error = true;
            sd.status_msg   = "Image not loaded";
            return;
        }

        switch (sd.format) {
        case ImageSaveDialog::Format::PNG:
            ok = save_png_impl(path, img);
            break;
        case ImageSaveDialog::Format::BMP:
            ok = save_bmp_impl(path, img);
            break;
        case ImageSaveDialog::Format::PPM:
            ok = save_ppm_impl(path, img,
                               sd.ppm_mode == ImageSaveDialog::PpmMode::Binary,
                               sd.ppm_bits);
            break;
        }
    } else if (target == ImageSaveDialog::Target::Diff) {
        const ImageEntry& imgA = state.images[state.swap_images ? 1 : 0];
        const ImageEntry& imgB = state.images[state.swap_images ? 0 : 1];

        if (!imgA.loaded || !imgB.loaded) {
            sd.status_error = true;
            sd.status_msg   = "Both images must be loaded for Diff";
            return;
        }

        auto diff_rgba8 = compute_diff_cpu(imgA, imgB, state.diff);
        int  w = std::min(imgA.width,  imgB.width);
        int  h = std::min(imgA.height, imgB.height);

        ImageEntry tmp;
        tmp.width    = w;
        tmp.height   = h;
        tmp.channels = 4;
        tmp.loaded   = true;
        tmp.is_hdr   = false;
        tmp.pixels   = std::move(diff_rgba8);

        switch (sd.format) {
        case ImageSaveDialog::Format::PNG:
            ok = save_png_impl(path, tmp);
            break;
        case ImageSaveDialog::Format::BMP:
            ok = save_bmp_impl(path, tmp);
            break;
        case ImageSaveDialog::Format::PPM:
            ok = save_ppm_impl(path, tmp,
                               sd.ppm_mode == ImageSaveDialog::PpmMode::Binary,
                               sd.ppm_bits);
            break;
        }
    }

    if (!ok) {
        sd.status_error = true;
        sd.status_msg   = "Error: Failed to write " + path;
    } else {
        // Extract just the filename for display
        auto slash = path_last_sep(path);
        std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        sd.status_msg = "Saved: " + fname;
    }
}

// ─── Context menu: native save dialog ────────────────────────────────────────

static void SDLCALL context_save_callback(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* cs = static_cast<ContextSaveState*>(userdata);
    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        cs->save_path    = filelist[0];
        cs->save_pending = true;
    }
}

void open_context_save_dialog(AppState& state, int target_type) {
    state.context_save.save_target = target_type;
    SDL_DialogFileFilter filters[] = {
        {"PNG image", "png"},
        {"BMP image", "bmp"},
        {"PPM image", "ppm"},
    };
    SDL_ShowSaveFileDialog(context_save_callback, &state.context_save,
                           state.window, filters, 3, nullptr);
}
