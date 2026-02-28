#include "chart_export.h"
#include "soft_renderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <numeric>

// Font path for soft-renderer text (same as ImGui font)
#ifndef IMGUI_FONT_DIR
#define IMGUI_FONT_DIR "."
#endif

// ─── Internal stat helpers ────────────────────────────────────────────────────

static ChannelStats compute_channel_stats_u8(const uint8_t* pixels, int npix, int ch_offset)
{
    ChannelStats s;
    if (npix <= 0) return s;
    s.count = npix;

    uint32_t hist[256] = {};
    double sum = 0.0, sum_sq = 0.0;
    int mn = 255, mx = 0;
    for (int i = 0; i < npix; ++i) {
        int v = pixels[i * 4 + ch_offset];
        hist[v]++;
        sum    += v;
        sum_sq += (double)v * v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    s.min_val        = mn;
    s.max_val        = mx;
    s.dynamic_range  = mx - mn;
    s.mean           = sum / npix;
    s.energy         = sum_sq / npix;
    s.rms            = std::sqrt(s.energy);

    double ent = 0.0;
    for (int b = 0; b < 256; ++b) {
        if (hist[b] > 0) {
            double p = (double)hist[b] / npix;
            ent -= p * std::log2(p);
        }
    }
    s.entropy = ent;

    int half = npix / 2, cum = 0;
    s.median = 0.0;
    for (int b = 0; b < 256; ++b) {
        cum += hist[b];
        if (cum >= half) { s.median = b; break; }
    }

    double mu = s.mean, sd2 = 0.0, sd3 = 0.0, sd4 = 0.0;
    for (int i = 0; i < npix; ++i) {
        double d  = pixels[i * 4 + ch_offset] - mu;
        double d2 = d * d;
        sd2 += d2; sd3 += d2 * d; sd4 += d2 * d2;
    }
    s.variance = sd2 / npix;
    s.std_dev  = std::sqrt(s.variance);
    if (s.std_dev > 1e-12) {
        double sig3 = s.std_dev * s.std_dev * s.std_dev;
        double sig4 = sig3 * s.std_dev;
        s.skewness = (sd3 / npix) / sig3;
        s.kurtosis = (sd4 / npix) / sig4 - 3.0;
    }
    return s;
}

static ChannelStats compute_channel_stats_f32(const float* pixels, int npix, int ch_offset)
{
    ChannelStats s;
    if (npix <= 0) return s;
    s.count = npix;

    double sum = 0.0, sum_sq = 0.0;
    float mn = pixels[ch_offset], mx = pixels[ch_offset];
    for (int i = 0; i < npix; ++i) {
        float v = pixels[i * 4 + ch_offset];
        sum    += v;
        sum_sq += (double)v * v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    s.min_val        = mn;
    s.max_val        = mx;
    s.dynamic_range  = mx - mn;
    s.mean           = sum / npix;
    s.energy         = sum_sq / npix;
    s.rms            = std::sqrt(s.energy);

    const int NBINS = 1024;
    std::vector<uint32_t> hist(NBINS, 0);
    float range = mx - mn;
    if (range < 1e-12f) range = 1e-12f;
    for (int i = 0; i < npix; ++i) {
        float v = pixels[i * 4 + ch_offset];
        int b = (int)((v - mn) / range * (NBINS - 1) + 0.5f);
        b = std::clamp(b, 0, NBINS - 1);
        hist[b]++;
    }

    double ent = 0.0;
    for (int b = 0; b < NBINS; ++b) {
        if (hist[b] > 0) {
            double p = (double)hist[b] / npix;
            ent -= p * std::log2(p);
        }
    }
    s.entropy = ent;

    int half = npix / 2, cum = 0;
    s.median = mn;
    for (int b = 0; b < NBINS; ++b) {
        cum += hist[b];
        if (cum >= half) {
            s.median = mn + (double)b / (NBINS - 1) * range;
            break;
        }
    }

    double mu = s.mean, sd2 = 0.0, sd3 = 0.0, sd4 = 0.0;
    for (int i = 0; i < npix; ++i) {
        double d  = pixels[i * 4 + ch_offset] - mu;
        double d2 = d * d;
        sd2 += d2; sd3 += d2 * d; sd4 += d2 * d2;
    }
    s.variance = sd2 / npix;
    s.std_dev  = std::sqrt(s.variance);
    if (s.std_dev > 1e-15) {
        double sig3 = s.std_dev * s.std_dev * s.std_dev;
        double sig4 = sig3 * s.std_dev;
        s.skewness = (sd3 / npix) / sig3;
        s.kurtosis = (sd4 / npix) / sig4 - 3.0;
    }
    return s;
}

// ─── Public stat computation ──────────────────────────────────────────────────

ImageStats compute_image_stats(const ImageEntry& img)
{
    ImageStats st;
    if (!img.loaded) return st;
    int npix = img.width * img.height;
    if (img.is_hdr && !img.pixels_f32.empty()) {
        for (int c = 0; c < 3; ++c)
            st.ch[c] = compute_channel_stats_f32(img.pixels_f32.data(), npix, c);
    } else if (!img.pixels.empty()) {
        for (int c = 0; c < 3; ++c)
            st.ch[c] = compute_channel_stats_u8(img.pixels.data(), npix, c);
    } else return st;
    st.valid = true;
    return st;
}

ImageStats compute_diff_stats(const ImageEntry& imgA, const ImageEntry& imgB,
                              DiffExtraStats& extra)
{
    ImageStats st;
    int npix = std::min(imgA.width * imgA.height, imgB.width * imgB.height);
    if (npix <= 0) return st;

    bool use_hdr = imgA.is_hdr && imgB.is_hdr &&
                   !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool use_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    if (use_hdr) {
        std::vector<float> diff_buf(npix * 4, 0.0f);
        for (int i = 0; i < npix; ++i)
            for (int c = 0; c < 3; ++c)
                diff_buf[i*4+c] = std::fabsf(imgA.pixels_f32[i*4+c] - imgB.pixels_f32[i*4+c]);
        for (int c = 0; c < 3; ++c) {
            st.ch[c] = compute_channel_stats_f32(diff_buf.data(), npix, c);
            // MSE, PSNR, MAE, MaxError for diff stats
            double mse = 0.0, mae = 0.0, maxe = 0.0;
            for (int i = 0; i < npix; ++i) {
                double d = std::fabsf(imgA.pixels_f32[i*4+c] - imgB.pixels_f32[i*4+c]);
                mse += d * d; mae += d;
                if (d > maxe) maxe = d;
            }
            extra.mse[c] = mse / npix;
            extra.mae[c] = mae / npix;
            extra.max_error[c] = maxe;
            extra.psnr[c] = (extra.mse[c] > 1e-15)
                ? 20.0 * std::log10(1.0) - 10.0 * std::log10(extra.mse[c])
                : std::numeric_limits<double>::infinity();
        }
    } else if (use_u8) {
        std::vector<uint8_t> diff_buf(npix * 4, 0);
        for (int i = 0; i < npix; ++i)
            for (int c = 0; c < 3; ++c)
                diff_buf[i*4+c] = static_cast<uint8_t>(
                    std::abs((int)imgA.pixels[i*4+c] - (int)imgB.pixels[i*4+c]));
        for (int c = 0; c < 3; ++c) {
            st.ch[c] = compute_channel_stats_u8(diff_buf.data(), npix, c);
            double mse = 0.0, mae = 0.0, maxe = 0.0;
            for (int i = 0; i < npix; ++i) {
                double d = std::abs((int)imgA.pixels[i*4+c] - (int)imgB.pixels[i*4+c]);
                mse += d * d; mae += d;
                if (d > maxe) maxe = d;
            }
            extra.mse[c] = mse / npix;
            extra.mae[c] = mae / npix;
            extra.max_error[c] = maxe;
            // PSNR relative to 255
            extra.psnr[c] = (extra.mse[c] > 1e-6)
                ? 20.0 * std::log10(255.0) - 10.0 * std::log10(extra.mse[c])
                : std::numeric_limits<double>::infinity();
        }
    } else return st;

    st.valid = true;
    return st;
}

// ─── Histogram extraction ─────────────────────────────────────────────────────

HistogramData extract_histogram(const ImageEntry& img)
{
    HistogramData d;
    if (!img.loaded) return d;

    const char* fname = img.path.empty() ? "(loaded)"
                      : (img.path.rfind('/') != std::string::npos
                          ? img.path.c_str() + img.path.rfind('/') + 1
                          : img.path.c_str());
    d.label = fname;

    int npix = img.width * img.height;
    if (img.is_hdr && !img.pixels_f32.empty()) {
        float max_v = 1.0f;
        for (int p = 0; p < npix; ++p) {
            float r = img.pixels_f32[p*4+0];
            float g = img.pixels_f32[p*4+1];
            float b = img.pixels_f32[p*4+2];
            if (r > max_v) max_v = r;
            if (g > max_v) max_v = g;
            if (b > max_v) max_v = b;
        }
        for (int p = 0; p < npix; ++p) {
            d.r[std::clamp((int)(img.pixels_f32[p*4+0] / max_v * 255.0f), 0, 255)]++;
            d.g[std::clamp((int)(img.pixels_f32[p*4+1] / max_v * 255.0f), 0, 255)]++;
            d.b[std::clamp((int)(img.pixels_f32[p*4+2] / max_v * 255.0f), 0, 255)]++;
        }
    } else if (!img.pixels.empty()) {
        for (int p = 0; p < npix; ++p) {
            d.r[img.pixels[p*4+0]]++;
            d.g[img.pixels[p*4+1]]++;
            d.b[img.pixels[p*4+2]]++;
        }
    }
    return d;
}

HistogramData extract_diff_histogram(const ImageEntry& imgA, const ImageEntry& imgB)
{
    HistogramData d;
    d.label = "Diff |A-B|";
    int npix = std::min(imgA.width * imgA.height, imgB.width * imgB.height);
    if (npix <= 0) return d;

    bool use_hdr = imgA.is_hdr && imgB.is_hdr &&
                   !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool use_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    if (use_hdr) {
        float max_v = 1e-6f;
        for (int p = 0; p < npix; ++p) {
            float dr = std::fabsf(imgA.pixels_f32[p*4+0] - imgB.pixels_f32[p*4+0]);
            float dg = std::fabsf(imgA.pixels_f32[p*4+1] - imgB.pixels_f32[p*4+1]);
            float db = std::fabsf(imgA.pixels_f32[p*4+2] - imgB.pixels_f32[p*4+2]);
            if (dr > max_v) max_v = dr;
            if (dg > max_v) max_v = dg;
            if (db > max_v) max_v = db;
        }
        for (int p = 0; p < npix; ++p) {
            d.r[std::clamp((int)(std::fabsf(imgA.pixels_f32[p*4+0] - imgB.pixels_f32[p*4+0]) / max_v * 255.0f), 0, 255)]++;
            d.g[std::clamp((int)(std::fabsf(imgA.pixels_f32[p*4+1] - imgB.pixels_f32[p*4+1]) / max_v * 255.0f), 0, 255)]++;
            d.b[std::clamp((int)(std::fabsf(imgA.pixels_f32[p*4+2] - imgB.pixels_f32[p*4+2]) / max_v * 255.0f), 0, 255)]++;
        }
    } else if (use_u8) {
        for (int p = 0; p < npix; ++p) {
            d.r[std::clamp(std::abs((int)imgA.pixels[p*4+0] - (int)imgB.pixels[p*4+0]), 0, 255)]++;
            d.g[std::clamp(std::abs((int)imgA.pixels[p*4+1] - (int)imgB.pixels[p*4+1]), 0, 255)]++;
            d.b[std::clamp(std::abs((int)imgA.pixels[p*4+2] - (int)imgB.pixels[p*4+2]), 0, 255)]++;
        }
    }
    return d;
}

// ─── Line-cut extraction ──────────────────────────────────────────────────────

LineCutData extract_hline_cut(const ImageEntry& img, const ViewportState& vp)
{
    LineCutData d;
    if (!img.loaded) return d;

    int row = static_cast<int>(std::round(-vp.pan_y + img.height * 0.5f));
    row = std::clamp(row, 0, img.height - 1);
    d.position = row;

    const char* fname = img.path.empty() ? "(loaded)"
                      : (img.path.rfind('/') != std::string::npos
                          ? img.path.c_str() + img.path.rfind('/') + 1
                          : img.path.c_str());
    char lbl[256];
    std::snprintf(lbl, sizeof(lbl), "%s  row=%d", fname, row);
    d.label = lbl;

    int n = img.width;
    if (n < 1) return d;
    d.r.resize(n); d.g.resize(n); d.b.resize(n);

    if (img.is_hdr && !img.pixels_f32.empty()) {
        float max_v = 1.0f;
        for (int x = 0; x < n; ++x) {
            int idx = (row * img.width + x) * 4;
            d.r[x] = img.pixels_f32[idx+0];
            d.g[x] = img.pixels_f32[idx+1];
            d.b[x] = img.pixels_f32[idx+2];
            if (d.r[x] > max_v) max_v = d.r[x];
            if (d.g[x] > max_v) max_v = d.g[x];
            if (d.b[x] > max_v) max_v = d.b[x];
        }
        for (int x = 0; x < n; ++x) {
            d.r[x] /= max_v; d.g[x] /= max_v; d.b[x] /= max_v;
        }
        d.max_val = max_v;
        d.is_hdr  = true;
    } else if (!img.pixels.empty()) {
        for (int x = 0; x < n; ++x) {
            int idx = (row * img.width + x) * 4;
            d.r[x] = img.pixels[idx+0] / 255.0f;
            d.g[x] = img.pixels[idx+1] / 255.0f;
            d.b[x] = img.pixels[idx+2] / 255.0f;
        }
        d.max_val = 255.0f;
    }
    return d;
}

LineCutData extract_vline_cut(const ImageEntry& img, const ViewportState& vp)
{
    LineCutData d;
    if (!img.loaded) return d;

    int col = static_cast<int>(std::round(-vp.pan_x + img.width * 0.5f));
    col = std::clamp(col, 0, img.width - 1);
    d.position = col;

    const char* fname = img.path.empty() ? "(loaded)"
                      : (img.path.rfind('/') != std::string::npos
                          ? img.path.c_str() + img.path.rfind('/') + 1
                          : img.path.c_str());
    char lbl[256];
    std::snprintf(lbl, sizeof(lbl), "%s  col=%d", fname, col);
    d.label = lbl;

    int n = img.height;
    if (n < 1) return d;
    d.r.resize(n); d.g.resize(n); d.b.resize(n);

    if (img.is_hdr && !img.pixels_f32.empty()) {
        float max_v = 1.0f;
        for (int y = 0; y < n; ++y) {
            int idx = (y * img.width + col) * 4;
            d.r[y] = img.pixels_f32[idx+0];
            d.g[y] = img.pixels_f32[idx+1];
            d.b[y] = img.pixels_f32[idx+2];
            if (d.r[y] > max_v) max_v = d.r[y];
            if (d.g[y] > max_v) max_v = d.g[y];
            if (d.b[y] > max_v) max_v = d.b[y];
        }
        for (int y = 0; y < n; ++y) {
            d.r[y] /= max_v; d.g[y] /= max_v; d.b[y] /= max_v;
        }
        d.max_val = max_v;
        d.is_hdr  = true;
    } else if (!img.pixels.empty()) {
        for (int y = 0; y < n; ++y) {
            int idx = (y * img.width + col) * 4;
            d.r[y] = img.pixels[idx+0] / 255.0f;
            d.g[y] = img.pixels[idx+1] / 255.0f;
            d.b[y] = img.pixels[idx+2] / 255.0f;
        }
        d.max_val = 255.0f;
    }
    return d;
}

LineCutData extract_diff_hline_cut(const ImageEntry& imgA, const ImageEntry& imgB,
                                   const ViewportState& vp)
{
    LineCutData d;
    int row = static_cast<int>(std::round(-vp.pan_y + imgA.height * 0.5f));
    row = std::clamp(row, 0, std::min(imgA.height, imgB.height) - 1);
    d.position = row;

    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "Diff |A-B|  row=%d", row);
    d.label = lbl;

    int n = std::min(imgA.width, imgB.width);
    if (n < 1) return d;
    d.r.resize(n); d.g.resize(n); d.b.resize(n);

    bool use_hdr = imgA.is_hdr && imgB.is_hdr &&
                   !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool use_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    if (use_hdr) {
        float max_v = 1e-6f;
        for (int x = 0; x < n; ++x) {
            int ia = (row * imgA.width + x) * 4;
            int ib = (row * imgB.width + x) * 4;
            d.r[x] = std::fabsf(imgA.pixels_f32[ia+0] - imgB.pixels_f32[ib+0]);
            d.g[x] = std::fabsf(imgA.pixels_f32[ia+1] - imgB.pixels_f32[ib+1]);
            d.b[x] = std::fabsf(imgA.pixels_f32[ia+2] - imgB.pixels_f32[ib+2]);
            if (d.r[x] > max_v) max_v = d.r[x];
            if (d.g[x] > max_v) max_v = d.g[x];
            if (d.b[x] > max_v) max_v = d.b[x];
        }
        for (int x = 0; x < n; ++x) {
            d.r[x] /= max_v; d.g[x] /= max_v; d.b[x] /= max_v;
        }
        d.max_val = max_v;
        d.is_hdr  = true;
    } else if (use_u8) {
        for (int x = 0; x < n; ++x) {
            int ia = (row * imgA.width + x) * 4;
            int ib = (row * imgB.width + x) * 4;
            d.r[x] = std::abs((int)imgA.pixels[ia+0] - (int)imgB.pixels[ib+0]) / 255.0f;
            d.g[x] = std::abs((int)imgA.pixels[ia+1] - (int)imgB.pixels[ib+1]) / 255.0f;
            d.b[x] = std::abs((int)imgA.pixels[ia+2] - (int)imgB.pixels[ib+2]) / 255.0f;
        }
        d.max_val = 255.0f;
    }
    return d;
}

LineCutData extract_diff_vline_cut(const ImageEntry& imgA, const ImageEntry& imgB,
                                   const ViewportState& vp)
{
    LineCutData d;
    int col = static_cast<int>(std::round(-vp.pan_x + imgA.width * 0.5f));
    col = std::clamp(col, 0, std::min(imgA.width, imgB.width) - 1);
    d.position = col;

    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "Diff |A-B|  col=%d", col);
    d.label = lbl;

    int n = std::min(imgA.height, imgB.height);
    if (n < 1) return d;
    d.r.resize(n); d.g.resize(n); d.b.resize(n);

    bool use_hdr = imgA.is_hdr && imgB.is_hdr &&
                   !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
    bool use_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

    if (use_hdr) {
        float max_v = 1e-6f;
        for (int y = 0; y < n; ++y) {
            int ia = (y * imgA.width + col) * 4;
            int ib = (y * imgB.width + col) * 4;
            d.r[y] = std::fabsf(imgA.pixels_f32[ia+0] - imgB.pixels_f32[ib+0]);
            d.g[y] = std::fabsf(imgA.pixels_f32[ia+1] - imgB.pixels_f32[ib+1]);
            d.b[y] = std::fabsf(imgA.pixels_f32[ia+2] - imgB.pixels_f32[ib+2]);
            if (d.r[y] > max_v) max_v = d.r[y];
            if (d.g[y] > max_v) max_v = d.g[y];
            if (d.b[y] > max_v) max_v = d.b[y];
        }
        for (int y = 0; y < n; ++y) {
            d.r[y] /= max_v; d.g[y] /= max_v; d.b[y] /= max_v;
        }
        d.max_val = max_v;
        d.is_hdr  = true;
    } else if (use_u8) {
        for (int y = 0; y < n; ++y) {
            int ia = (y * imgA.width + col) * 4;
            int ib = (y * imgB.width + col) * 4;
            d.r[y] = std::abs((int)imgA.pixels[ia+0] - (int)imgB.pixels[ib+0]) / 255.0f;
            d.g[y] = std::abs((int)imgA.pixels[ia+1] - (int)imgB.pixels[ib+1]) / 255.0f;
            d.b[y] = std::abs((int)imgA.pixels[ia+2] - (int)imgB.pixels[ib+2]) / 255.0f;
        }
        d.max_val = 255.0f;
    }
    return d;
}

// ─── SoftRenderer font helper ─────────────────────────────────────────────────

static bool load_chart_font(SoftRenderer& sr, float size)
{
    return sr.load_font(IMGUI_FONT_DIR "/Roboto-Medium.ttf", size);
}

// ─── Histogram PNG rendering ──────────────────────────────────────────────────

// Render a single channel's histogram bars into the SoftRenderer.
// chart_x/y/w/h: the chart area in canvas pixels.
static void render_hist_channel(SoftRenderer& sr,
                                 const uint32_t* hist,
                                 int cx, int cy, int cw, int ch,
                                 uint8_t cr, uint8_t cg, uint8_t cb)
{
    // Background
    sr.fill_rect(cx, cy, cw, ch, 30, 30, 30);

    // Gridlines at 25/50/75/100%
    for (int t = 1; t <= 4; ++t) {
        int gy = cy + ch - (ch * t / 4);
        sr.hline(cx, cx + cw - 1, gy, 60, 60, 60, 120);
    }

    // Max log for normalisation
    float max_log = 1.0f;
    for (int b = 0; b < 256; ++b) {
        float lv = std::log1p((float)hist[b]);
        if (lv > max_log) max_log = lv;
    }

    // Bars
    float bar_w = (float)cw / 256.0f;
    for (int b = 0; b < 256; ++b) {
        float h_frac = std::log1p((float)hist[b]) / max_log;
        int   bh     = static_cast<int>(h_frac * ch);
        if (bh < 1) continue;
        int x0 = cx + static_cast<int>(b * bar_w);
        int x1 = cx + static_cast<int>((b + 1) * bar_w);
        if (x1 <= x0) x1 = x0 + 1;
        sr.fill_rect(x0, cy + ch - bh, x1 - x0, bh, cr, cg, cb, 200);
    }

    // Border
    sr.draw_rect(cx, cy, cw, ch, 80, 80, 80);
}

static bool render_histogram_to_sr(SoftRenderer& sr,
                                    const HistogramData& data,
                                    int ch_idx)  // 0=R, 1=G, 2=B, -1=combined
{
    // Margins
    int ml = 80, mr = 20, mt = 40, mb = 50;
    int cw = sr.width()  - ml - mr;
    int ch = sr.height() - mt - mb;
    if (cw < 10 || ch < 10) return false;

    const uint32_t* hists[3] = { data.r, data.g, data.b };
    const uint8_t   cols[3][3] = {
        {255, 60, 60},   // R
        {60, 255, 60},   // G
        {60, 60, 255}    // B
    };

    if (ch_idx >= 0) {
        // Single channel
        render_hist_channel(sr, hists[ch_idx],
                            ml, mt, cw, ch,
                            cols[ch_idx][0], cols[ch_idx][1], cols[ch_idx][2]);
    } else {
        // Combined: sub-divide vertically into 3 sub-charts
        int sub_h  = (ch - 20) / 3;
        int gap    = (ch - sub_h * 3) / 2;
        const char* labels[3] = {"R", "G", "B"};
        for (int c = 0; c < 3; ++c) {
            int cy = mt + c * (sub_h + gap);
            render_hist_channel(sr, hists[c],
                                ml, cy, cw, sub_h,
                                cols[c][0], cols[c][1], cols[c][2]);
            // Channel label
            sr.draw_text(ml + 4, cy + 4, labels[c],
                         cols[c][0], cols[c][1], cols[c][2]);
        }
        // X-axis ticks (drawn once below last chart)
        int last_cy  = mt + 2 * (sub_h + gap);
        int last_cy2 = last_cy + sub_h;
        static const int x_ticks[] = { 0, 64, 128, 192, 255 };
        for (int v : x_ticks) {
            int px = ml + static_cast<int>((float)v / 255.0f * cw);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", v);
            int tw = sr.text_width(buf);
            sr.draw_text(px - tw/2, last_cy2 + 4, buf, 180, 180, 180);
        }
        // Title
        sr.draw_text(ml, 8, data.label.c_str(), 200, 200, 200);
        return true;
    }

    // X-axis ticks (0, 64, 128, 192, 255)
    static const int x_ticks[] = { 0, 64, 128, 192, 255 };
    for (int v : x_ticks) {
        int px = ml + static_cast<int>((float)v / 255.0f * cw);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", v);
        int tw = sr.text_width(buf);
        sr.draw_text(px - tw/2, mt + ch + 4, buf, 180, 180, 180);
    }

    // Y-axis: max count label
    uint32_t max_cnt = 0;
    const uint32_t* h = (ch_idx >= 0) ? hists[ch_idx] : data.r;
    for (int b = 0; b < 256; ++b)
        if (h[b] > max_cnt) max_cnt = h[b];
    char cnt_buf[32];
    std::snprintf(cnt_buf, sizeof(cnt_buf), "%u", max_cnt);
    sr.draw_text(4, mt, cnt_buf, 180, 180, 180);

    // Title
    sr.draw_text(ml, 8, data.label.c_str(), 200, 200, 200);
    return true;
}

bool export_histogram_png(const HistogramData& data, const std::string& path,
                          int w, int h, bool separate_channels)
{
    auto save_one = [&](const std::string& p, int ch_idx) -> bool {
        SoftRenderer sr(w, h);
        sr.clear(20, 20, 20);
        load_chart_font(sr, 14.0f);
        render_histogram_to_sr(sr, data, ch_idx);
        return sr.save_png(p.c_str());
    };

    if (!separate_channels) {
        return save_one(path, -1);  // combined
    }

    // Separate R/G/B: strip extension and append _R/_G/_B
    std::string base = path;
    std::string ext;
    auto dot = base.rfind('.');
    if (dot != std::string::npos) {
        ext  = base.substr(dot);
        base = base.substr(0, dot);
    }
    bool ok = true;
    const char* sfx[3] = {"_R", "_G", "_B"};
    for (int c = 0; c < 3; ++c) {
        std::string p2 = base + sfx[c] + (ext.empty() ? ".png" : ext);
        ok &= save_one(p2, c);
    }
    return ok;
}

// ─── Line-cut PNG rendering ───────────────────────────────────────────────────

static bool render_linecut_to_sr(SoftRenderer& sr,
                                  const LineCutData& data,
                                  int ch_idx,   // 0=R, 1=G, 2=B, -1=all-stacked
                                  const char* x_label)
{
    (void)x_label;
    int ml = 70, mr = 20, mt = 40, mb = 50;
    int tot_w = sr.width()  - ml - mr;
    int tot_h = sr.height() - mt - mb;
    if (tot_w < 10 || tot_h < 10) return false;

    int n = static_cast<int>(data.r.size());
    if (n < 1) return false;

    const float* ch_data[3] = { data.r.data(), data.g.data(), data.b.data() };
    const uint8_t cols[3][3] = {
        {255, 80, 80}, {80, 255, 80}, {80, 120, 255}
    };

    auto draw_one = [&](int cy, int ch, int c) {
        // Background
        sr.fill_rect(ml, cy, tot_w, ch, 30, 30, 30);
        // Gridlines
        for (int t = 1; t <= 4; ++t) {
            int gy = cy + ch - (ch * t / 4);
            sr.hline(ml, ml + tot_w - 1, gy, 60, 60, 60, 120);
        }
        // Border
        sr.draw_rect(ml, cy, tot_w, ch, 80, 80, 80);

        // Polyline
        if (n >= 2) {
            std::vector<float> xs(n), ys(n);
            for (int i = 0; i < n; ++i) {
                xs[i] = ml + (float)i / (float)(n - 1) * tot_w;
                float v = std::clamp(ch_data[c][i], 0.0f, 1.0f);
                ys[i] = cy + ch - v * ch;
            }
            sr.draw_polyline(xs.data(), ys.data(), n,
                             cols[c][0], cols[c][1], cols[c][2], 220, 1.5f);
        }

        // Y-axis label (max value)
        char buf[32];
        float ymax = data.max_val;
        if (data.is_hdr)
            std::snprintf(buf, sizeof(buf), "%.2f", ymax);
        else
            std::snprintf(buf, sizeof(buf), "%d", (int)ymax);
        sr.draw_text(4, cy, buf, 180, 180, 180);
        sr.draw_text(4, cy + ch/2, "0", 180, 180, 180);

        // X-axis labels
        static const int steps[] = { 0, 25, 50, 75, 100 };
        for (int pct : steps) {
            int xi = n * pct / 100;
            if (xi >= n) xi = n - 1;
            int px = ml + (n > 1 ? static_cast<int>((float)xi / (n - 1) * tot_w) : 0);
            char xbuf[16];
            std::snprintf(xbuf, sizeof(xbuf), "%d", xi);
            int tw = sr.text_width(xbuf);
            sr.draw_text(px - tw/2, cy + ch + 4, xbuf, 180, 180, 180);
        }
    };

    if (ch_idx >= 0) {
        draw_one(mt, tot_h, ch_idx);
    } else {
        // Stack R/G/B
        int sub_h = (tot_h - 20) / 3;
        int gap   = (tot_h - sub_h * 3) / 2;
        const char* ch_labels[3] = {"R", "G", "B"};
        for (int c = 0; c < 3; ++c) {
            int cy = mt + c * (sub_h + gap);
            draw_one(cy, sub_h, c);
            sr.draw_text(ml + 4, cy + 2, ch_labels[c], cols[c][0], cols[c][1], cols[c][2]);
        }
    }

    // Title
    sr.draw_text(ml, 8, data.label.c_str(), 200, 200, 200);
    return true;
}

bool export_linecut_png(const LineCutData& data, const std::string& path,
                        int w, int h, bool separate_channels,
                        const char* x_label)
{
    auto save_one = [&](const std::string& p, int ch_idx) -> bool {
        SoftRenderer sr(w, h);
        sr.clear(20, 20, 20);
        load_chart_font(sr, 14.0f);
        render_linecut_to_sr(sr, data, ch_idx, x_label);
        return sr.save_png(p.c_str());
    };

    if (!separate_channels) {
        return save_one(path, -1);
    }

    std::string base = path, ext;
    auto dot = base.rfind('.');
    if (dot != std::string::npos) { ext = base.substr(dot); base = base.substr(0, dot); }
    bool ok = true;
    const char* sfx[3] = {"_R", "_G", "_B"};
    for (int c = 0; c < 3; ++c) {
        ok &= save_one(base + sfx[c] + (ext.empty() ? ".png" : ext), c);
    }
    return ok;
}

// ─── CSV export ───────────────────────────────────────────────────────────────

bool export_histogram_csv(const HistogramData& data, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "bin,R,G,B\n";
    for (int b = 0; b < 256; ++b)
        f << b << "," << data.r[b] << "," << data.g[b] << "," << data.b[b] << "\n";
    return f.good();
}

bool export_linecut_csv(const LineCutData& data, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "position,R,G,B\n";
    int n = static_cast<int>(data.r.size());
    for (int i = 0; i < n; ++i) {
        float r, g, b;
        if (data.is_hdr) {
            r = data.r[i] * data.max_val;
            g = data.g[i] * data.max_val;
            b = data.b[i] * data.max_val;
            f << i << "," << r << "," << g << "," << b << "\n";
        } else {
            r = data.r[i] * data.max_val;
            g = data.g[i] * data.max_val;
            b = data.b[i] * data.max_val;
            f << i << ","
              << static_cast<int>(r + 0.5f) << ","
              << static_cast<int>(g + 0.5f) << ","
              << static_cast<int>(b + 0.5f) << "\n";
        }
    }
    return f.good();
}

bool export_stats_csv(const ImageStats& stats, const std::string& path,
                      const DiffExtraStats* extra)
{
    std::ofstream f(path);
    if (!f) return false;

    f << "Metric,R,G,B,Description\n";

    auto row = [&](const char* name, auto r, auto g, auto b, const char* desc) {
        f << name << "," << r << "," << g << "," << b << "," << desc << "\n";
    };

    auto fmt = [](double v) -> std::string {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        return buf;
    };

    const ChannelStats* ch = stats.ch;
    row("Count",         ch[0].count,             ch[1].count,             ch[2].count,             "Pixel count");
    row("Min",           fmt(ch[0].min_val).c_str(), fmt(ch[1].min_val).c_str(), fmt(ch[2].min_val).c_str(), "Minimum value");
    row("Max",           fmt(ch[0].max_val).c_str(), fmt(ch[1].max_val).c_str(), fmt(ch[2].max_val).c_str(), "Maximum value");
    row("DynamicRange",  fmt(ch[0].dynamic_range).c_str(), fmt(ch[1].dynamic_range).c_str(), fmt(ch[2].dynamic_range).c_str(), "Max - Min");
    row("Mean",          fmt(ch[0].mean).c_str(),   fmt(ch[1].mean).c_str(),   fmt(ch[2].mean).c_str(),   "Arithmetic mean");
    row("Variance",      fmt(ch[0].variance).c_str(), fmt(ch[1].variance).c_str(), fmt(ch[2].variance).c_str(), "Population variance");
    row("StdDev",        fmt(ch[0].std_dev).c_str(), fmt(ch[1].std_dev).c_str(), fmt(ch[2].std_dev).c_str(), "Standard deviation");
    row("Median",        fmt(ch[0].median).c_str(), fmt(ch[1].median).c_str(), fmt(ch[2].median).c_str(), "Median value");
    row("Skewness",      fmt(ch[0].skewness).c_str(), fmt(ch[1].skewness).c_str(), fmt(ch[2].skewness).c_str(), "Distribution skewness");
    row("Kurtosis",      fmt(ch[0].kurtosis).c_str(), fmt(ch[1].kurtosis).c_str(), fmt(ch[2].kurtosis).c_str(), "Excess kurtosis");
    row("Energy",        fmt(ch[0].energy).c_str(), fmt(ch[1].energy).c_str(), fmt(ch[2].energy).c_str(), "Mean squared value");
    row("RMS",           fmt(ch[0].rms).c_str(),    fmt(ch[1].rms).c_str(),    fmt(ch[2].rms).c_str(),    "Root mean square");
    row("Entropy",       fmt(ch[0].entropy).c_str(), fmt(ch[1].entropy).c_str(), fmt(ch[2].entropy).c_str(), "Shannon entropy (bits)");

    if (extra) {
        row("MSE",       fmt(extra->mse[0]).c_str(),       fmt(extra->mse[1]).c_str(),       fmt(extra->mse[2]).c_str(),       "Mean squared error");
        row("PSNR",      fmt(extra->psnr[0]).c_str(),      fmt(extra->psnr[1]).c_str(),      fmt(extra->psnr[2]).c_str(),      "Peak signal-to-noise ratio (dB)");
        row("MAE",       fmt(extra->mae[0]).c_str(),       fmt(extra->mae[1]).c_str(),       fmt(extra->mae[2]).c_str(),       "Mean absolute error");
        row("MaxError",  fmt(extra->max_error[0]).c_str(), fmt(extra->max_error[1]).c_str(), fmt(extra->max_error[2]).c_str(), "Maximum absolute error");
    }

    return f.good();
}
