#include "soft_renderer.h"

#include <stb_truetype.h>
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

SoftRenderer::SoftRenderer(int width, int height)
    : w_(width), h_(height)
{
    buf_.assign(static_cast<size_t>(w_) * h_ * 4, 0);
}

SoftRenderer::~SoftRenderer()
{
    delete font_info_;
}

// ─── Blend pixel (RGBA over RGB) ─────────────────────────────────────────────

void SoftRenderer::blend_pixel(int x, int y,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
    size_t idx = (static_cast<size_t>(y) * w_ + x) * 4;
    if (a == 255) {
        buf_[idx+0] = r; buf_[idx+1] = g; buf_[idx+2] = b; buf_[idx+3] = 255;
    } else if (a > 0) {
        float fa     = a / 255.0f;
        float fa_inv = 1.0f - fa;
        buf_[idx+0] = static_cast<uint8_t>(buf_[idx+0] * fa_inv + r * fa + 0.5f);
        buf_[idx+1] = static_cast<uint8_t>(buf_[idx+1] * fa_inv + g * fa + 0.5f);
        buf_[idx+2] = static_cast<uint8_t>(buf_[idx+2] * fa_inv + b * fa + 0.5f);
        buf_[idx+3] = 255;
    }
}

// ─── Primitives ───────────────────────────────────────────────────────────────

void SoftRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    size_t npix = static_cast<size_t>(w_) * h_;
    for (size_t i = 0; i < npix; ++i) {
        buf_[i*4+0] = r; buf_[i*4+1] = g; buf_[i*4+2] = b; buf_[i*4+3] = a;
    }
}

void SoftRenderer::fill_rect(int x, int y, int w, int h,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int x0 = std::max(x, 0),      y0 = std::max(y, 0);
    int x1 = std::min(x + w, w_), y1 = std::min(y + h, h_);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px)
            blend_pixel(px, py, r, g, b, a);
}

void SoftRenderer::draw_rect(int x, int y, int w, int h,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    hline(x, x+w-1, y,     r, g, b, a);
    hline(x, x+w-1, y+h-1, r, g, b, a);
    vline(x,     y, y+h-1, r, g, b, a);
    vline(x+w-1, y, y+h-1, r, g, b, a);
}

void SoftRenderer::hline(int x0, int x1, int y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (y < 0 || y >= h_) return;
    x0 = std::max(x0, 0); x1 = std::min(x1, w_ - 1);
    for (int px = x0; px <= x1; ++px)
        blend_pixel(px, y, r, g, b, a);
}

void SoftRenderer::vline(int x, int y0, int y1,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= w_) return;
    y0 = std::max(y0, 0); y1 = std::min(y1, h_ - 1);
    for (int py = y0; py <= y1; ++py)
        blend_pixel(x, py, r, g, b, a);
}

// ─── draw_line ────────────────────────────────────────────────────────────────
// Bresenham with thickness support (perpendicular widening).

void SoftRenderer::draw_line(float x0, float y0, float x1, float y1,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                              float thickness)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 0.5f) {
        blend_pixel(static_cast<int>(x0 + 0.5f), static_cast<int>(y0 + 0.5f), r, g, b, a);
        return;
    }

    // Perpendicular unit vector
    float px = -dy / len, py = dx / len;
    int half = static_cast<int>(std::ceil(thickness * 0.5f));

    // Bresenham integer endpoints
    int ix0 = static_cast<int>(x0 + 0.5f), iy0 = static_cast<int>(y0 + 0.5f);
    int ix1 = static_cast<int>(x1 + 0.5f), iy1 = static_cast<int>(y1 + 0.5f);

    int ddx = std::abs(ix1 - ix0), ddy = std::abs(iy1 - iy0);
    int sx = (ix1 > ix0) ? 1 : -1, sy = (iy1 > iy0) ? 1 : -1;
    int err = ddx - ddy;

    int cx = ix0, cy = iy0;
    while (true) {
        // Draw perpendicular span for thickness
        for (int t = -half; t <= half; ++t) {
            int qx = cx + static_cast<int>(px * t + 0.5f);
            int qy = cy + static_cast<int>(py * t + 0.5f);
            blend_pixel(qx, qy, r, g, b, a);
        }
        if (cx == ix1 && cy == iy1) break;
        int e2 = 2 * err;
        if (e2 > -ddy) { err -= ddy; cx += sx; }
        if (e2 <  ddx) { err += ddx; cy += sy; }
    }
}

void SoftRenderer::draw_polyline(const float* xs, const float* ys, int count,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                  float thickness)
{
    for (int i = 0; i + 1 < count; ++i)
        draw_line(xs[i], ys[i], xs[i+1], ys[i+1], r, g, b, a, thickness);
}

// ─── Font loading ─────────────────────────────────────────────────────────────

bool SoftRenderer::load_font(const char* ttf_path, float pixel_height)
{
    // Read the TTF file
    std::ifstream fs(ttf_path, std::ios::binary | std::ios::ate);
    if (!fs.is_open()) return false;
    auto fsize = fs.tellg();
    if (fsize <= 0) return false;
    font_data_.resize(static_cast<size_t>(fsize));
    fs.seekg(0);
    fs.read(reinterpret_cast<char*>(font_data_.data()), fsize);
    if (!fs) return false;

    if (!font_info_) font_info_ = new stbtt_fontinfo;
    if (!stbtt_InitFont(font_info_, font_data_.data(), 0)) {
        delete font_info_; font_info_ = nullptr;
        font_data_.clear();
        return false;
    }

    font_size_  = pixel_height;
    font_scale_ = stbtt_ScaleForPixelHeight(font_info_, pixel_height);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(font_info_, &ascent, &descent, &line_gap);
    font_ascent_   = static_cast<int>(ascent   * font_scale_ + 0.5f);
    font_descent_  = static_cast<int>(descent  * font_scale_ - 0.5f);
    font_line_gap_ = static_cast<int>(line_gap * font_scale_ + 0.5f);
    font_line_h_   = font_ascent_ - font_descent_ + font_line_gap_;

    return true;
}

// ─── Text rendering ───────────────────────────────────────────────────────────

void SoftRenderer::draw_text(int x, int y, const char* text,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (!font_info_ || !text) return;

    int cx = x;
    int baseline = y + font_ascent_;

    for (const char* p = text; *p; ++p) {
        int cp = static_cast<unsigned char>(*p);

        int adv, lsb;
        stbtt_GetCodepointHMetrics(font_info_, cp, &adv, &lsb);

        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBox(font_info_, cp, font_scale_, font_scale_,
                                    &bx0, &by0, &bx1, &by1);

        int gw = bx1 - bx0, gh = by1 - by0;
        if (gw > 0 && gh > 0) {
            std::vector<uint8_t> bitmap(static_cast<size_t>(gw) * gh);
            stbtt_MakeCodepointBitmap(font_info_, bitmap.data(),
                                       gw, gh, gw, font_scale_, font_scale_, cp);

            int dst_x = cx + bx0;
            int dst_y = baseline + by0;

            for (int gy = 0; gy < gh; ++gy) {
                for (int gx = 0; gx < gw; ++gx) {
                    uint8_t alpha = bitmap[static_cast<size_t>(gy) * gw + gx];
                    if (alpha == 0) continue;
                    // Modulate glyph alpha by requested alpha
                    uint8_t ea = static_cast<uint8_t>((uint32_t)alpha * a / 255u);
                    blend_pixel(dst_x + gx, dst_y + gy, r, g, b, ea);
                }
            }
        }

        cx += static_cast<int>(adv * font_scale_ + 0.5f);

        // Kerning for next character pair
        if (*(p+1)) {
            int kern = stbtt_GetCodepointKernAdvance(font_info_, cp, (unsigned char)*(p+1));
            cx += static_cast<int>(kern * font_scale_ + 0.5f);
        }
    }
}

int SoftRenderer::text_width(const char* text) const
{
    if (!font_info_ || !text) return 0;
    int w = 0;
    for (const char* p = text; *p; ++p) {
        int cp = static_cast<unsigned char>(*p);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font_info_, cp, &adv, &lsb);
        w += static_cast<int>(adv * font_scale_ + 0.5f);
        if (*(p+1)) {
            int kern = stbtt_GetCodepointKernAdvance(font_info_, cp, (unsigned char)*(p+1));
            w += static_cast<int>(kern * font_scale_ + 0.5f);
        }
    }
    return w;
}

// ─── Save PNG ─────────────────────────────────────────────────────────────────

bool SoftRenderer::save_png(const char* path) const
{
    return stbi_write_png(path, w_, h_, 4, buf_.data(), w_ * 4) != 0;
}
