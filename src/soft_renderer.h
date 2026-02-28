#pragma once

#include <cstdint>
#include <vector>
#include <string>

// Forward-declare stbtt_fontinfo to avoid including stb_truetype.h here
struct stbtt_fontinfo;

// ─── SoftRenderer ─────────────────────────────────────────────────────────────
// CPU-side RGBA8 canvas for off-screen chart/graph rendering.
// Supports basic drawing primitives and stb_truetype text rendering.

class SoftRenderer {
public:
    SoftRenderer(int width, int height);
    ~SoftRenderer();

    int width()  const { return w_; }
    int height() const { return h_; }

    // Fill entire canvas
    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Filled rectangle
    void fill_rect(int x, int y, int w, int h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Outline rectangle
    void draw_rect(int x, int y, int w, int h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Horizontal line (fast path)
    void hline(int x0, int x1, int y,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Vertical line (fast path)
    void vline(int x, int y0, int y1,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Anti-aliased line with thickness
    void draw_line(float x0, float y0, float x1, float y1,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                   float thickness = 1.0f);

    // Draw connected polyline
    void draw_polyline(const float* xs, const float* ys, int count,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                       float thickness = 1.5f);

    // Load font from .ttf file for text rendering
    bool load_font(const char* ttf_path, float pixel_height);

    // Draw text at pixel position (top-left origin)
    void draw_text(int x, int y, const char* text,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Measure text width in pixels (requires loaded font)
    int text_width(const char* text) const;

    // Returns font line height (ascent + descent + line_gap)
    int font_line_height() const { return font_line_h_; }

    // Save canvas as PNG
    bool save_png(const char* path) const;

private:
    int w_, h_;
    std::vector<uint8_t> buf_;   // RGBA8, row-major

    // stb_truetype font state
    std::vector<uint8_t>  font_data_;
    stbtt_fontinfo*       font_info_ = nullptr;
    float font_scale_ = 0.0f;
    float font_size_  = 0.0f;
    int   font_ascent_    = 0;
    int   font_descent_   = 0;
    int   font_line_gap_  = 0;
    int   font_line_h_    = 0;

    // Alpha-blend a single pixel
    void blend_pixel(int x, int y,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};
