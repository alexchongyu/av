#pragma once

#include "app.h"

#include <cstdint>
#include <vector>
#include <string>

// ─── Statistics structures (shared with chart_windows.cpp) ───────────────────

struct ChannelStats {
    int    count         = 0;
    double min_val       = 0.0;
    double max_val       = 0.0;
    double dynamic_range = 0.0;
    double mean          = 0.0;
    double variance      = 0.0;
    double std_dev       = 0.0;
    double median        = 0.0;
    double skewness      = 0.0;
    double kurtosis      = 0.0;
    double energy        = 0.0;
    double rms           = 0.0;
    double entropy       = 0.0;
};

struct ImageStats {
    ChannelStats ch[3];
    bool valid = false;
};

struct DiffExtraStats {
    double mse[3]       = {};
    double psnr[3]      = {};
    double mae[3]       = {};
    double max_error[3] = {};
};

// ─── Chart data structures ───────────────────────────────────────────────────

struct HistogramData {
    uint32_t    r[256] = {};
    uint32_t    g[256] = {};
    uint32_t    b[256] = {};
    std::string label;
};

struct LineCutData {
    std::vector<float> r, g, b;  // normalised [0,1] values
    float       max_val  = 255.0f;
    bool        is_hdr   = false;
    int         position = 0;    // row (H-cut) or col (V-cut) index
    std::string label;
};

// ─── Stats computation ───────────────────────────────────────────────────────

ImageStats compute_image_stats(const ImageEntry& img);
ImageStats compute_diff_stats(const ImageEntry& imgA, const ImageEntry& imgB,
                              DiffExtraStats& extra);

// ─── Histogram data extraction ───────────────────────────────────────────────

HistogramData extract_histogram(const ImageEntry& img);
HistogramData extract_diff_histogram(const ImageEntry& imgA, const ImageEntry& imgB);

// ─── Line-cut data extraction ────────────────────────────────────────────────

LineCutData extract_hline_cut(const ImageEntry& img, const ViewportState& vp);
LineCutData extract_vline_cut(const ImageEntry& img, const ViewportState& vp);
LineCutData extract_diff_hline_cut(const ImageEntry& imgA, const ImageEntry& imgB,
                                   const ViewportState& vp);
LineCutData extract_diff_vline_cut(const ImageEntry& imgA, const ImageEntry& imgB,
                                   const ViewportState& vp);

// ─── PNG export functions ────────────────────────────────────────────────────

// Export histogram as PNG.
// separate_channels=true → saves {base}_R.png, {base}_G.png, {base}_B.png
bool export_histogram_png(const HistogramData& data, const std::string& path,
                          int w, int h, bool separate_channels);

// Export line cut (H or V) as PNG.
// x_label: "Pixel Column" or "Pixel Row"
bool export_linecut_png(const LineCutData& data, const std::string& path,
                        int w, int h, bool separate_channels,
                        const char* x_label = "Pixel");

// ─── CSV export functions ────────────────────────────────────────────────────

// Histogram CSV: bin,R,G,B  (256 rows)
bool export_histogram_csv(const HistogramData& data, const std::string& path);

// Line cut CSV: position,R,G,B  (n rows, de-normalised values)
bool export_linecut_csv(const LineCutData& data, const std::string& path);

// Statistics CSV: Metric,R,G,B,Description
// extra: optional diff metrics (may be nullptr for single-image stats)
bool export_stats_csv(const ImageStats& stats, const std::string& path,
                      const DiffExtraStats* extra = nullptr);

// ─── ROI statistics ───────────────────────────────────────────────────────────

// ROI 영역 통계 계산 (rx,ry: top-left, rw,rh: 크기, 이미지 픽셀 좌표)
ImageStats compute_roi_stats(const ImageEntry& img, int rx, int ry, int rw, int rh);
ImageStats compute_roi_diff_stats(const ImageEntry& a, const ImageEntry& b,
                                   int rx, int ry, int rw, int rh,
                                   DiffExtraStats& extra);

// ─── Scatter Plot data ────────────────────────────────────────────────────────

struct ScatterPlotData {
    // 샘플링된 픽셀 쌍: (A_value, B_value) each channel normalized [0,1]
    std::vector<float> r_a, r_b;  // R channel
    std::vector<float> g_a, g_b;  // G channel
    std::vector<float> b_a, b_b;  // B channel
    int total_pixels = 0;
    int sampled      = 0;
    bool valid       = false;
};

// max_samples: 성능을 위해 픽셀을 랜덤 샘플링 (0 = 모두)
ScatterPlotData extract_scatter_plot(const ImageEntry& a, const ImageEntry& b,
                                     int max_samples = 20000);
