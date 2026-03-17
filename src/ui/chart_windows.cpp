#include "chart_windows.h"
#include "../app.h"
#include "../chart_export.h"
#include "../path_utils.h"

#include <imgui.h>

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <numeric>
#include <string>

// ─── helpers ──────────────────────────────────────────────────────────────────

static const char* basename_of(const char* path) {
    return path_basename(path);
}

// Format integer with comma separators (e.g. 1234567 → "1,234,567")
static std::string fmt_comma(int64_t n) {
    std::string s = std::to_string(n);
    int ins = (int)s.size() - 3;
    while (ins > 0) { s.insert((size_t)ins, 1, ','); ins -= 3; }
    return s;
}

// Draw dark background + border + horizontal gridlines (25%, 50%, 75%, 100%)
static void draw_chart_bg(ImDrawList* dl, ImVec2 origin, float w, float h) {
    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                      IM_COL32(30, 30, 30, 220));
    // Gridlines at 25 / 50 / 75 / 100%
    for (int t = 1; t <= 4; ++t) {
        float y = origin.y + h * (1.0f - t * 0.25f);
        dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + w, y),
                    IM_COL32(60, 60, 60, 120));
    }
    dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                IM_COL32(80, 80, 80, 180));
}

// Map sample index → x pixel, handling downsampled case
static inline float sample_x(int i, int num_pts, float origin_x, float chart_w) {
    if (num_pts <= 1) return origin_x + chart_w * 0.5f;
    return origin_x + (float)i / (float)(num_pts - 1) * chart_w;
}

// Draw an RGB polyline for a sequence of normalised [0,1] values.
// Downsamples src to num_pts evenly-spaced samples.
static void draw_channel_line(ImDrawList* dl,
                              const float* src, int src_n,
                              int num_pts,
                              ImVec2 origin, float chart_w, float chart_h,
                              ImU32 color)
{
    if (num_pts < 2 || src_n < 1) return;
    std::vector<ImVec2> pts;
    pts.reserve(num_pts);
    for (int i = 0; i < num_pts; ++i) {
        int si = (src_n > 1) ? (int)((float)i / (float)(num_pts - 1) * (float)(src_n - 1) + 0.5f) : 0;
        si = std::clamp(si, 0, src_n - 1);
        float x = sample_x(i, num_pts, origin.x, chart_w);
        float y = origin.y + chart_h * (1.0f - std::clamp(src[si], 0.0f, 1.0f));
        pts.push_back(ImVec2(x, y));
    }
    dl->AddPolyline(pts.data(), (int)pts.size(), color, 0, 1.5f);
}

// Compute "nice" tick values for a range [0..range] with at most max_ticks intervals
static void calc_nice_ticks(int range, int max_ticks, std::vector<int>& out) {
    if (range <= 0) { out.clear(); out.push_back(0); return; }
    float raw_step = (float)range / (float)max_ticks;
    float mag = std::pow(10.0f, std::floor(std::log10(raw_step)));
    float frac = raw_step / mag;
    float nice;
    if (frac <= 1.5f) nice = 1.0f;
    else if (frac <= 3.5f) nice = 2.0f;
    else if (frac <= 7.5f) nice = 5.0f;
    else nice = 10.0f;
    int step = std::max((int)(nice * mag), 1);
    out.clear();
    for (int v = 0; v <= range; v += step) out.push_back(v);
    if (out.back() != range) out.push_back(range);
}

// ─── render_histogram_window ──────────────────────────────────────────────────

void render_histogram_window(AppState& state) {
    if (!state.show_histogram) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.85f),
            ImGuiCond_Appearing);
    }
    if (!ImGui::Begin("Histogram", &state.show_histogram)) {
        ImGui::End();
        return;
    }

    // Dynamic chart size based on available window space
    int num_sections_h = 0;
    for (int ii = 0; ii < 2; ++ii) if (state.images[ii].loaded) num_sections_h++;
    {
        bool both = state.images[0].loaded && state.images[1].loaded;
        bool diff = (state.diff.mode != DiffState::Mode::None);
        if (both && diff) num_sections_h++;
    }
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    // Compute y-margin dynamically based on max possible histogram count
    float y_margin = 40.0f;
    {
        int64_t max_possible = 0;
        for (int ii = 0; ii < 2; ++ii) {
            if (state.images[ii].loaded) {
                int64_t np = (int64_t)state.images[ii].width * state.images[ii].height;
                if (np > max_possible) max_possible = np;
            }
        }
        if (max_possible > 0) {
            std::string s = fmt_comma(max_possible);
            y_margin = std::max(ImGui::CalcTextSize(s.c_str()).x + 8.0f, 40.0f);
        }
    }
    float chart_w = std::max(avail_w - 16.0f - y_margin, 200.0f);
    int total_charts = num_sections_h * 3;
    // overhead: 104px per section (labels/spacing) + 16px per channel x 3 for X-axis labels
    float overhead = (float)num_sections_h * 152.0f;
    float ch_h = (total_charts > 0)
        ? std::max((avail_h - overhead) / (float)total_charts, 80.0f)
        : 80.0f;

    const ImU32 ch_colors[3] = {
        IM_COL32(255, 60,  60,  180),
        IM_COL32(60,  255, 60,  180),
        IM_COL32(60,  60,  255, 180)
    };
    const char* ch_names[3] = { "R", "G", "B" };

    // X-axis ticks for histogram (0..255)
    static const int hist_x_ticks[] = { 0, 32, 64, 96, 128, 160, 192, 224, 255 };
    static const int hist_x_nticks = 9;

    bool any = false;

    // Helper: draw R/G/B separated charts for given histogram data
    auto draw_hist_section = [&](const char* label,
                                 const uint32_t hist_r[256],
                                 const uint32_t hist_g[256],
                                 const uint32_t hist_b[256],
                                 float cw, float chh)
    {
        ImGui::Text("%s", label);
        const uint32_t* ch_data[3] = { hist_r, hist_g, hist_b };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ch_origins[3];
        float  ch_heights[3];
        for (int c = 0; c < 3; ++c) {
            // Per-channel max log normalisation (each channel self-contained)
            float max_log = 1.0f;
            uint32_t max_count = 0;
            for (int b = 0; b < 256; ++b) {
                float lv = std::log1p((float)ch_data[c][b]);
                if (lv > max_log) max_log = lv;
                if (ch_data[c][b] > max_count) max_count = ch_data[c][b];
            }
            ImGui::Text("  %s", ch_names[c]);
            // Offset cursor right for Y-axis label space
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + y_margin);
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cw, chh));
            ch_origins[c] = origin;
            ch_heights[c] = chh;
            draw_chart_bg(dl, origin, cw, chh);
            float bar_w = cw / 256.0f;
            for (int b = 0; b < 256; ++b) {
                float h_frac = std::log1p((float)ch_data[c][b]) / max_log;
                float bh = h_frac * chh;
                if (bh < 0.5f) continue;
                float x0 = origin.x + b * bar_w;
                float x1 = x0 + std::max(bar_w, 1.0f);
                float y0 = origin.y + chh - bh;
                float y1 = origin.y + chh;
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ch_colors[c]);
            }
            // Y-axis labels: actual count via log-reverse (expm1)
            for (int t = 1; t <= 4; ++t) {
                float y = origin.y + chh * (1.0f - t * 0.25f);
                int64_t cnt = (t == 4) ? (int64_t)max_count
                                       : (int64_t)std::round(std::expm1(t * 0.25f * max_log));
                std::string s = fmt_comma(cnt);
                ImVec2 ts = ImGui::CalcTextSize(s.c_str());
                dl->AddText(ImVec2(origin.x - ts.x - 4.0f, y - ts.y * 0.5f),
                            IM_COL32(180, 180, 180, 200), s.c_str());
            }
            // X-axis labels (0, 32, 64, ..., 255) — center-aligned below chart
            for (int ti = 0; ti < hist_x_nticks; ++ti) {
                int v = hist_x_ticks[ti];
                float x = origin.x + (float)v / 255.0f * cw;
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", v);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(x - ts.x * 0.5f, origin.y + chh + 2.0f),
                            IM_COL32(180, 180, 180, 200), buf);
            }
            // Reserve space for X-axis label (replaces Spacing)
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
        }
        // ── Hover crosshair & tooltip ──
        {
            static const ImVec4 tip_cols[3] = {
                ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                ImVec4(0.3f, 0.3f, 1.0f, 1.0f)
            };
            const uint32_t* tip_data[3] = { hist_r, hist_g, hist_b };
            ImVec2 mouse = ImGui::GetMousePos();
            for (int c = 0; c < 3; ++c) {
                ImVec2 org = ch_origins[c];
                float  h   = ch_heights[c];
                if (!ImGui::IsMouseHoveringRect(org, ImVec2(org.x + cw, org.y + h))) continue;
                int bin = std::clamp((int)((mouse.x - org.x) / cw * 256.0f), 0, 255);
                float bx = org.x + (bin + 0.5f) / 256.0f * cw;
                for (int cc = 0; cc < 3; ++cc)
                    dl->AddLine(ImVec2(bx, ch_origins[cc].y),
                                ImVec2(bx, ch_origins[cc].y + ch_heights[cc]),
                                IM_COL32(255, 255, 255, 120), 1.0f);
                ImGui::BeginTooltip();
                ImGui::Text("Bin: %d", bin);
                ImGui::Separator();
                for (int cc = 0; cc < 3; ++cc) {
                    std::string s = fmt_comma((int64_t)tip_data[cc][bin]);
                    ImGui::TextColored(tip_cols[cc], "%s: %s", ch_names[cc], s.c_str());
                }
                ImGui::EndTooltip();
                break;
            }
        }
    };

    for (int ii = 0; ii < 2; ++ii) {
        const ImageEntry& img = state.images[ii];
        if (!img.loaded) continue;
        any = true;

        const char* fname = img.path.empty() ? "(loaded)" : basename_of(img.path.c_str());
        char label[256];
        std::snprintf(label, sizeof(label), "%s: %s", ii == 0 ? "A" : "B", fname);

        // ── Build 256-bin histogram ──
        uint32_t hist_r[256]{}, hist_g[256]{}, hist_b[256]{};

        if (!img.pixels_f32.empty()) {
            float max_v = 1.0f;
            int npix = img.width * img.height;
            for (int p = 0; p < npix; ++p) {
                float r = img.pixels_f32[p * 4 + 0];
                float g = img.pixels_f32[p * 4 + 1];
                float b = img.pixels_f32[p * 4 + 2];
                if (r > max_v) max_v = r;
                if (g > max_v) max_v = g;
                if (b > max_v) max_v = b;
            }
            for (int p = 0; p < npix; ++p) {
                int br = std::clamp((int)(img.pixels_f32[p*4+0] / max_v * 255.0f), 0, 255);
                int bg = std::clamp((int)(img.pixels_f32[p*4+1] / max_v * 255.0f), 0, 255);
                int bb = std::clamp((int)(img.pixels_f32[p*4+2] / max_v * 255.0f), 0, 255);
                hist_r[br]++; hist_g[bg]++; hist_b[bb]++;
            }
        } else if (!img.pixels.empty()) {
            int npix = img.width * img.height;
            for (int p = 0; p < npix; ++p) {
                hist_r[img.pixels[p*4+0]]++;
                hist_g[img.pixels[p*4+1]]++;
                hist_b[img.pixels[p*4+2]]++;
            }
        }

        draw_hist_section(label, hist_r, hist_g, hist_b, chart_w, ch_h);
        ImGui::Spacing();
    }

    // ── Diff histogram: abs(A-B) ──
    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    bool diff_on     = (state.diff.mode != DiffState::Mode::None);
    if (both_loaded && diff_on) {
        any = true;
        uint32_t hist_r[256]{}, hist_g[256]{}, hist_b[256]{};
        const ImageEntry& imgA = state.images[0];
        const ImageEntry& imgB = state.images[1];
        int npix_a = imgA.width * imgA.height;
        int npix_b = imgB.width * imgB.height;
        int npix   = std::min(npix_a, npix_b);

        bool use_hdr = (imgA.is_hdr && imgB.is_hdr &&
                        !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty());
        bool use_u8  = (!imgA.pixels.empty() && !imgB.pixels.empty());

        if (use_hdr) {
            float max_v = 1e-6f;
            for (int p = 0; p < npix; ++p) {
                float dr = std::fabs(imgA.pixels_f32[p*4+0] - imgB.pixels_f32[p*4+0]);
                float dg = std::fabs(imgA.pixels_f32[p*4+1] - imgB.pixels_f32[p*4+1]);
                float db = std::fabs(imgA.pixels_f32[p*4+2] - imgB.pixels_f32[p*4+2]);
                if (dr > max_v) max_v = dr;
                if (dg > max_v) max_v = dg;
                if (db > max_v) max_v = db;
            }
            for (int p = 0; p < npix; ++p) {
                int br = std::clamp((int)(std::fabs(imgA.pixels_f32[p*4+0] - imgB.pixels_f32[p*4+0]) / max_v * 255.0f), 0, 255);
                int bg = std::clamp((int)(std::fabs(imgA.pixels_f32[p*4+1] - imgB.pixels_f32[p*4+1]) / max_v * 255.0f), 0, 255);
                int bb = std::clamp((int)(std::fabs(imgA.pixels_f32[p*4+2] - imgB.pixels_f32[p*4+2]) / max_v * 255.0f), 0, 255);
                hist_r[br]++; hist_g[bg]++; hist_b[bb]++;
            }
        } else if (use_u8) {
            for (int p = 0; p < npix; ++p) {
                int dr = std::abs((int)imgA.pixels[p*4+0] - (int)imgB.pixels[p*4+0]);
                int dg = std::abs((int)imgA.pixels[p*4+1] - (int)imgB.pixels[p*4+1]);
                int db = std::abs((int)imgA.pixels[p*4+2] - (int)imgB.pixels[p*4+2]);
                hist_r[std::clamp(dr, 0, 255)]++;
                hist_g[std::clamp(dg, 0, 255)]++;
                hist_b[std::clamp(db, 0, 255)]++;
            }
        }

        if (use_hdr || use_u8) {
            draw_hist_section("Diff |A-B|", hist_r, hist_g, hist_b, chart_w, ch_h);
            ImGui::Spacing();
        }
    }

    // ── Compare A/B overlay mode ──
    if (state.images[0].loaded && state.images[1].loaded) {
        ImGui::Separator();
        ImGui::Checkbox("Compare A/B", &state.histogram_compare);
        if (state.histogram_compare) {
            // Build histograms for A and B
            uint32_t hist_a[3][256]{}, hist_b[3][256]{};
            const ImageEntry& imgA = state.images[0];
            const ImageEntry& imgB = state.images[1];

            if (!imgA.pixels.empty()) {
                int npix = imgA.width * imgA.height;
                for (int p = 0; p < npix; ++p) {
                    hist_a[0][imgA.pixels[p*4+0]]++;
                    hist_a[1][imgA.pixels[p*4+1]]++;
                    hist_a[2][imgA.pixels[p*4+2]]++;
                }
            }
            if (!imgB.pixels.empty()) {
                int npix = imgB.width * imgB.height;
                for (int p = 0; p < npix; ++p) {
                    hist_b[0][imgB.pixels[p*4+0]]++;
                    hist_b[1][imgB.pixels[p*4+1]]++;
                    hist_b[2][imgB.pixels[p*4+2]]++;
                }
            }

            ImGui::Text("Compare A (magenta) / B (yellow)");
            const ImU32 col_a = IM_COL32(255, 0, 255, 120);
            const ImU32 col_b = IM_COL32(255, 255, 0, 120);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            for (int c = 0; c < 3; ++c) {
                // Find common max (log scale)
                float max_log = 1.0f;
                uint32_t max_count_a = 0, max_count_b = 0;
                for (int bin = 0; bin < 256; ++bin) {
                    float lv_a = std::log1p((float)hist_a[c][bin]);
                    float lv_b = std::log1p((float)hist_b[c][bin]);
                    if (lv_a > max_log) max_log = lv_a;
                    if (lv_b > max_log) max_log = lv_b;
                    if (hist_a[c][bin] > max_count_a) max_count_a = hist_a[c][bin];
                    if (hist_b[c][bin] > max_count_b) max_count_b = hist_b[c][bin];
                }
                ImGui::Text("  %s", ch_names[c]);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + y_margin);
                ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(chart_w, ch_h));
                draw_chart_bg(dl, origin, chart_w, ch_h);

                float bar_w = chart_w / 256.0f;
                // Draw A bars (magenta)
                for (int bin = 0; bin < 256; ++bin) {
                    float h_frac = std::log1p((float)hist_a[c][bin]) / max_log;
                    float bh = h_frac * ch_h;
                    if (bh < 0.5f) continue;
                    float x0 = origin.x + bin * bar_w;
                    float x1 = x0 + std::max(bar_w, 1.0f);
                    dl->AddRectFilled(ImVec2(x0, origin.y + ch_h - bh),
                                      ImVec2(x1, origin.y + ch_h), col_a);
                }
                // Draw B bars (yellow)
                for (int bin = 0; bin < 256; ++bin) {
                    float h_frac = std::log1p((float)hist_b[c][bin]) / max_log;
                    float bh = h_frac * ch_h;
                    if (bh < 0.5f) continue;
                    float x0 = origin.x + bin * bar_w;
                    float x1 = x0 + std::max(bar_w, 1.0f);
                    dl->AddRectFilled(ImVec2(x0, origin.y + ch_h - bh),
                                      ImVec2(x1, origin.y + ch_h), col_b);
                }
                // Y-axis labels
                uint32_t max_count = std::max(max_count_a, max_count_b);
                for (int t = 1; t <= 4; ++t) {
                    float y = origin.y + ch_h * (1.0f - t * 0.25f);
                    int64_t cnt = (t == 4) ? (int64_t)max_count
                                           : (int64_t)std::round(std::expm1(t * 0.25f * max_log));
                    std::string s = fmt_comma(cnt);
                    ImVec2 ts = ImGui::CalcTextSize(s.c_str());
                    dl->AddText(ImVec2(origin.x - ts.x - 4.0f, y - ts.y * 0.5f),
                                IM_COL32(180, 180, 180, 200), s.c_str());
                }
                // X-axis labels
                for (int ti = 0; ti < hist_x_nticks; ++ti) {
                    int v = hist_x_ticks[ti];
                    float x = origin.x + (float)v / 255.0f * chart_w;
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "%d", v);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(x - ts.x * 0.5f, origin.y + ch_h + 2.0f),
                                IM_COL32(180, 180, 180, 200), buf);
                }
                ImGui::Dummy(ImVec2(0.0f, 16.0f));
            }
            // (hover tooltip for compare mode uses the existing per-section hover logic)
        }
        any = true;
    }

    if (!any) {
        ImGui::TextDisabled("(no image loaded)");
    }

    ImGui::End();
}

// ─── render_hline_cut_window ──────────────────────────────────────────────────

void render_hline_cut_window(AppState& state) {
    if (!state.show_hline_cut) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.85f),
            ImGuiCond_Appearing);
    }
    if (!ImGui::Begin("H-Line Cut", &state.show_hline_cut)) {
        ImGui::End();
        return;
    }

    // Dynamic chart size based on available window space
    int num_sections_hl = 0;
    for (int ii = 0; ii < 2; ++ii) if (state.images[ii].loaded) num_sections_hl++;
    {
        bool both = state.images[0].loaded && state.images[1].loaded;
        bool diff = (state.diff.mode != DiffState::Mode::None);
        if (both && diff) num_sections_hl++;
    }
    float avail_w_hl = ImGui::GetContentRegionAvail().x;
    float avail_h_hl = ImGui::GetContentRegionAvail().y;
    // Y-axis label: 40px left margin
    float chart_w = std::max(avail_w_hl - 16.0f - 40.0f, 200.0f);
    int total_charts_hl = num_sections_hl * 3;
    // overhead: 104px per section + 16px per channel x 3 for X-axis labels
    float overhead_hl = (float)num_sections_hl * 152.0f;
    float chart_h = (total_charts_hl > 0)
        ? std::max((avail_h_hl - overhead_hl) / (float)total_charts_hl, 80.0f)
        : 80.0f;

    const ImU32 ch_colors[3] = {
        IM_COL32(255, 80,  80,  200),
        IM_COL32(80,  255, 80,  200),
        IM_COL32(80,  120, 255, 200)
    };
    const char* ch_names[3] = { "R", "G", "B" };

    // Helper: draw R/G/B separated line charts for given channel data
    auto draw_line_section = [&](const char* label,
                                  const float* rv, const float* gv, const float* bv,
                                  int n, float cw, float chh,
                                  float max_val, bool is_hdr_fmt)
    {
        ImGui::Text("%s", label);
        const float* ch_data[3] = { rv, gv, bv };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int num_pts = std::min(n, (int)cw);
        ImVec2 ch_origins[3];
        float  ch_heights[3];

        // X-axis nice ticks for pixel coordinates
        std::vector<int> x_ticks;
        calc_nice_ticks(n > 1 ? n - 1 : 0, 7, x_ticks);

        for (int c = 0; c < 3; ++c) {
            ImGui::Text("  %s", ch_names[c]);
            // Offset cursor 40px right for Y-axis label space
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cw, chh));
            ch_origins[c] = origin;
            ch_heights[c] = chh;
            draw_chart_bg(dl, origin, cw, chh);
            if (num_pts >= 2) {
                draw_channel_line(dl, ch_data[c], n, num_pts, origin, cw, chh, ch_colors[c]);
            }
            // Y-axis labels: actual pixel values (de-normalised from max_val)
            char y_buf[32];
            for (int t = 1; t <= 4; ++t) {
                float y_pos = origin.y + chh * (1.0f - t * 0.25f);
                float val = t * 0.25f * max_val;
                if (is_hdr_fmt)
                    std::snprintf(y_buf, sizeof(y_buf), "%.2f", val);
                else
                    std::snprintf(y_buf, sizeof(y_buf), "%d", (int)std::roundf(val));
                ImVec2 ts = ImGui::CalcTextSize(y_buf);
                dl->AddText(ImVec2(origin.x - ts.x - 4.0f, y_pos - ts.y * 0.5f),
                            IM_COL32(180, 180, 180, 200), y_buf);
            }
            // X-axis labels (nice pixel coordinate ticks) — center-aligned below chart
            if (n > 1) {
                for (int v : x_ticks) {
                    float x = origin.x + (float)v / (float)(n - 1) * cw;
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%d", v);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(x - ts.x * 0.5f, origin.y + chh + 2.0f),
                                IM_COL32(180, 180, 180, 200), buf);
                }
            }
            // Reserve space for X-axis label
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
        }
        // ── Hover crosshair & tooltip ──
        if (n >= 1) {
            static const ImVec4 tip_cols[3] = {
                ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                ImVec4(0.3f, 0.3f, 1.0f, 1.0f)
            };
            const float* tip_data[3] = { rv, gv, bv };
            ImVec2 mouse = ImGui::GetMousePos();
            for (int c = 0; c < 3; ++c) {
                ImVec2 org = ch_origins[c];
                float  h   = ch_heights[c];
                if (!ImGui::IsMouseHoveringRect(org, ImVec2(org.x + cw, org.y + h))) continue;
                int idx = std::clamp((int)std::round((mouse.x - org.x) / cw * (n - 1)), 0, n - 1);
                float px = org.x + (n > 1 ? (float)idx / (float)(n - 1) : 0.5f) * cw;
                for (int cc = 0; cc < 3; ++cc)
                    dl->AddLine(ImVec2(px, ch_origins[cc].y),
                                ImVec2(px, ch_origins[cc].y + ch_heights[cc]),
                                IM_COL32(255, 255, 255, 120), 1.0f);
                ImGui::BeginTooltip();
                ImGui::Text("X: %d", idx);
                ImGui::Separator();
                for (int cc = 0; cc < 3; ++cc) {
                    float val = tip_data[cc][idx] * max_val;
                    if (is_hdr_fmt)
                        ImGui::TextColored(tip_cols[cc], "%s: %.4f", ch_names[cc], val);
                    else
                        ImGui::TextColored(tip_cols[cc], "%s: %d", ch_names[cc], (int)std::roundf(val));
                }
                ImGui::EndTooltip();
                break;
            }
        }
    };

    bool any = false;

    for (int ii = 0; ii < 2; ++ii) {
        const ImageEntry&    img = state.images[ii];
        const ViewportState& vp  = state.views[ii];
        if (!img.loaded) continue;
        any = true;

        // Row at canvas centre
        int row = (int)std::round(-vp.pan_y + img.height * 0.5f);
        row = std::clamp(row, 0, img.height - 1);

        const char* fname = img.path.empty() ? "(loaded)" : basename_of(img.path.c_str());
        char label[256];
        std::snprintf(label, sizeof(label), "%s: %s  row=%d", ii == 0 ? "A" : "B", fname, row);

        int n = img.width;
        if (n < 1) { ImGui::Spacing(); continue; }

        // Extract row pixel values, normalised to [0,1]
        std::vector<float> rv(n), gv(n), bv(n);
        float line_max = 255.0f;
        bool  line_hdr = false;

        if (!img.pixels_f32.empty()) {
            float max_v = 1.0f;
            for (int x = 0; x < n; ++x) {
                int idx = (row * img.width + x) * 4;
                rv[x] = img.pixels_f32[idx + 0];
                gv[x] = img.pixels_f32[idx + 1];
                bv[x] = img.pixels_f32[idx + 2];
                if (rv[x] > max_v) max_v = rv[x];
                if (gv[x] > max_v) max_v = gv[x];
                if (bv[x] > max_v) max_v = bv[x];
            }
            for (int x = 0; x < n; ++x) {
                rv[x] /= max_v; gv[x] /= max_v; bv[x] /= max_v;
            }
            line_max = max_v;
            line_hdr = true;
        } else if (!img.pixels.empty()) {
            for (int x = 0; x < n; ++x) {
                int idx = (row * img.width + x) * 4;
                rv[x] = img.pixels[idx + 0] / 255.0f;
                gv[x] = img.pixels[idx + 1] / 255.0f;
                bv[x] = img.pixels[idx + 2] / 255.0f;
            }
        }

        draw_line_section(label, rv.data(), gv.data(), bv.data(), n, chart_w, chart_h, line_max, line_hdr);
        ImGui::Spacing();
    }

    // ── Diff H-line cut: abs(A[row][x] - B[row][x]) ──
    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    bool diff_on     = (state.diff.mode != DiffState::Mode::None);
    if (both_loaded && diff_on) {
        any = true;
        const ImageEntry&    imgA = state.images[0];
        const ImageEntry&    imgB = state.images[1];
        const ViewportState& vp   = state.views[0];

        int row = (int)std::round(-vp.pan_y + imgA.height * 0.5f);
        row = std::clamp(row, 0, std::min(imgA.height, imgB.height) - 1);

        char label[64];
        std::snprintf(label, sizeof(label), "Diff |A-B|  row=%d", row);

        int n = std::min(imgA.width, imgB.width);
        if (n >= 1) {
            std::vector<float> rv(n), gv(n), bv(n);
            float line_max = 255.0f;
            bool  line_hdr = false;

            bool use_hdr = (!imgA.pixels_f32.empty() && !imgB.pixels_f32.empty());
            bool use_u8  = (!imgA.pixels.empty() && !imgB.pixels.empty());

            if (use_hdr) {
                float max_v = 1e-6f;
                for (int x = 0; x < n; ++x) {
                    int ia = (row * imgA.width + x) * 4;
                    int ib = (row * imgB.width + x) * 4;
                    rv[x] = std::fabs(imgA.pixels_f32[ia+0] - imgB.pixels_f32[ib+0]);
                    gv[x] = std::fabs(imgA.pixels_f32[ia+1] - imgB.pixels_f32[ib+1]);
                    bv[x] = std::fabs(imgA.pixels_f32[ia+2] - imgB.pixels_f32[ib+2]);
                    if (rv[x] > max_v) max_v = rv[x];
                    if (gv[x] > max_v) max_v = gv[x];
                    if (bv[x] > max_v) max_v = bv[x];
                }
                for (int x = 0; x < n; ++x) {
                    rv[x] /= max_v; gv[x] /= max_v; bv[x] /= max_v;
                }
                line_max = max_v;
                line_hdr = true;
            } else if (use_u8) {
                for (int x = 0; x < n; ++x) {
                    int ia = (row * imgA.width + x) * 4;
                    int ib = (row * imgB.width + x) * 4;
                    rv[x] = std::abs((int)imgA.pixels[ia+0] - (int)imgB.pixels[ib+0]) / 255.0f;
                    gv[x] = std::abs((int)imgA.pixels[ia+1] - (int)imgB.pixels[ib+1]) / 255.0f;
                    bv[x] = std::abs((int)imgA.pixels[ia+2] - (int)imgB.pixels[ib+2]) / 255.0f;
                }
            }

            if (use_hdr || use_u8) {
                draw_line_section(label, rv.data(), gv.data(), bv.data(), n, chart_w, chart_h, line_max, line_hdr);
                ImGui::Spacing();
            }
        }
    }

    if (!any) {
        ImGui::TextDisabled("(no image loaded)");
    }

    ImGui::End();
}

// ─── render_vline_cut_window ──────────────────────────────────────────────────

void render_vline_cut_window(AppState& state) {
    if (!state.show_vline_cut) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.85f),
            ImGuiCond_Appearing);
    }
    if (!ImGui::Begin("V-Line Cut", &state.show_vline_cut)) {
        ImGui::End();
        return;
    }

    // Dynamic chart size based on available window space
    int num_sections_vl = 0;
    for (int ii = 0; ii < 2; ++ii) if (state.images[ii].loaded) num_sections_vl++;
    {
        bool both = state.images[0].loaded && state.images[1].loaded;
        bool diff = (state.diff.mode != DiffState::Mode::None);
        if (both && diff) num_sections_vl++;
    }
    float avail_w_vl = ImGui::GetContentRegionAvail().x;
    float avail_h_vl = ImGui::GetContentRegionAvail().y;
    // Y-axis label: 40px left margin
    float chart_w = std::max(avail_w_vl - 16.0f - 40.0f, 200.0f);
    int total_charts_vl = num_sections_vl * 3;
    // overhead: 104px per section + 16px per channel x 3 for X-axis labels
    float overhead_vl = (float)num_sections_vl * 152.0f;
    float chart_h = (total_charts_vl > 0)
        ? std::max((avail_h_vl - overhead_vl) / (float)total_charts_vl, 80.0f)
        : 80.0f;

    const ImU32 ch_colors[3] = {
        IM_COL32(255, 80,  80,  200),
        IM_COL32(80,  255, 80,  200),
        IM_COL32(80,  120, 255, 200)
    };
    const char* ch_names[3] = { "R", "G", "B" };

    // Helper: draw R/G/B separated line charts for given channel data
    auto draw_line_section = [&](const char* label,
                                  const float* rv, const float* gv, const float* bv,
                                  int n, float cw, float chh,
                                  float max_val, bool is_hdr_fmt)
    {
        ImGui::Text("%s", label);
        const float* ch_data[3] = { rv, gv, bv };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int num_pts = std::min(n, (int)cw);
        ImVec2 ch_origins[3];
        float  ch_heights[3];

        // X-axis nice ticks for pixel coordinates
        std::vector<int> x_ticks;
        calc_nice_ticks(n > 1 ? n - 1 : 0, 7, x_ticks);

        for (int c = 0; c < 3; ++c) {
            ImGui::Text("  %s", ch_names[c]);
            // Offset cursor 40px right for Y-axis label space
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cw, chh));
            ch_origins[c] = origin;
            ch_heights[c] = chh;
            draw_chart_bg(dl, origin, cw, chh);
            if (num_pts >= 2) {
                draw_channel_line(dl, ch_data[c], n, num_pts, origin, cw, chh, ch_colors[c]);
            }
            // Y-axis labels: actual pixel values (de-normalised from max_val)
            char y_buf[32];
            for (int t = 1; t <= 4; ++t) {
                float y_pos = origin.y + chh * (1.0f - t * 0.25f);
                float val = t * 0.25f * max_val;
                if (is_hdr_fmt)
                    std::snprintf(y_buf, sizeof(y_buf), "%.2f", val);
                else
                    std::snprintf(y_buf, sizeof(y_buf), "%d", (int)std::roundf(val));
                ImVec2 ts = ImGui::CalcTextSize(y_buf);
                dl->AddText(ImVec2(origin.x - ts.x - 4.0f, y_pos - ts.y * 0.5f),
                            IM_COL32(180, 180, 180, 200), y_buf);
            }
            // X-axis labels (nice pixel coordinate ticks) — center-aligned below chart
            if (n > 1) {
                for (int v : x_ticks) {
                    float x = origin.x + (float)v / (float)(n - 1) * cw;
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%d", v);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(x - ts.x * 0.5f, origin.y + chh + 2.0f),
                                IM_COL32(180, 180, 180, 200), buf);
                }
            }
            // Reserve space for X-axis label
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
        }
        // ── Hover crosshair & tooltip ──
        if (n >= 1) {
            static const ImVec4 tip_cols[3] = {
                ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                ImVec4(0.3f, 0.3f, 1.0f, 1.0f)
            };
            const float* tip_data[3] = { rv, gv, bv };
            ImVec2 mouse = ImGui::GetMousePos();
            for (int c = 0; c < 3; ++c) {
                ImVec2 org = ch_origins[c];
                float  h   = ch_heights[c];
                if (!ImGui::IsMouseHoveringRect(org, ImVec2(org.x + cw, org.y + h))) continue;
                int idx = std::clamp((int)std::round((mouse.x - org.x) / cw * (n - 1)), 0, n - 1);
                float px = org.x + (n > 1 ? (float)idx / (float)(n - 1) : 0.5f) * cw;
                for (int cc = 0; cc < 3; ++cc)
                    dl->AddLine(ImVec2(px, ch_origins[cc].y),
                                ImVec2(px, ch_origins[cc].y + ch_heights[cc]),
                                IM_COL32(255, 255, 255, 120), 1.0f);
                ImGui::BeginTooltip();
                ImGui::Text("Y: %d", idx);
                ImGui::Separator();
                for (int cc = 0; cc < 3; ++cc) {
                    float val = tip_data[cc][idx] * max_val;
                    if (is_hdr_fmt)
                        ImGui::TextColored(tip_cols[cc], "%s: %.4f", ch_names[cc], val);
                    else
                        ImGui::TextColored(tip_cols[cc], "%s: %d", ch_names[cc], (int)std::roundf(val));
                }
                ImGui::EndTooltip();
                break;
            }
        }
    };

    bool any = false;

    for (int ii = 0; ii < 2; ++ii) {
        const ImageEntry&    img = state.images[ii];
        const ViewportState& vp  = state.views[ii];
        if (!img.loaded) continue;
        any = true;

        // Column at canvas centre
        int col = (int)std::round(-vp.pan_x + img.width * 0.5f);
        col = std::clamp(col, 0, img.width - 1);

        const char* fname = img.path.empty() ? "(loaded)" : basename_of(img.path.c_str());
        char label[256];
        std::snprintf(label, sizeof(label), "%s: %s  col=%d", ii == 0 ? "A" : "B", fname, col);

        int n = img.height;
        if (n < 1) { ImGui::Spacing(); continue; }

        // Extract column pixel values, normalised to [0,1]
        std::vector<float> rv(n), gv(n), bv(n);
        float line_max = 255.0f;
        bool  line_hdr = false;

        if (!img.pixels_f32.empty()) {
            float max_v = 1.0f;
            for (int y = 0; y < n; ++y) {
                int idx = (y * img.width + col) * 4;
                rv[y] = img.pixels_f32[idx + 0];
                gv[y] = img.pixels_f32[idx + 1];
                bv[y] = img.pixels_f32[idx + 2];
                if (rv[y] > max_v) max_v = rv[y];
                if (gv[y] > max_v) max_v = gv[y];
                if (bv[y] > max_v) max_v = bv[y];
            }
            for (int y = 0; y < n; ++y) {
                rv[y] /= max_v; gv[y] /= max_v; bv[y] /= max_v;
            }
            line_max = max_v;
            line_hdr = true;
        } else if (!img.pixels.empty()) {
            for (int y = 0; y < n; ++y) {
                int idx = (y * img.width + col) * 4;
                rv[y] = img.pixels[idx + 0] / 255.0f;
                gv[y] = img.pixels[idx + 1] / 255.0f;
                bv[y] = img.pixels[idx + 2] / 255.0f;
            }
        }

        draw_line_section(label, rv.data(), gv.data(), bv.data(), n, chart_w, chart_h, line_max, line_hdr);
        ImGui::Spacing();
    }

    // ── Diff V-line cut: abs(A[y][col] - B[y][col]) ──
    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    bool diff_on     = (state.diff.mode != DiffState::Mode::None);
    if (both_loaded && diff_on) {
        any = true;
        const ImageEntry&    imgA = state.images[0];
        const ImageEntry&    imgB = state.images[1];
        const ViewportState& vp   = state.views[0];

        int col = (int)std::round(-vp.pan_x + imgA.width * 0.5f);
        col = std::clamp(col, 0, std::min(imgA.width, imgB.width) - 1);

        char label[64];
        std::snprintf(label, sizeof(label), "Diff |A-B|  col=%d", col);

        int n = std::min(imgA.height, imgB.height);
        if (n >= 1) {
            std::vector<float> rv(n), gv(n), bv(n);
            float line_max = 255.0f;
            bool  line_hdr = false;

            bool use_hdr = (!imgA.pixels_f32.empty() && !imgB.pixels_f32.empty());
            bool use_u8  = (!imgA.pixels.empty() && !imgB.pixels.empty());

            if (use_hdr) {
                float max_v = 1e-6f;
                for (int y = 0; y < n; ++y) {
                    int ia = (y * imgA.width + col) * 4;
                    int ib = (y * imgB.width + col) * 4;
                    rv[y] = std::fabs(imgA.pixels_f32[ia+0] - imgB.pixels_f32[ib+0]);
                    gv[y] = std::fabs(imgA.pixels_f32[ia+1] - imgB.pixels_f32[ib+1]);
                    bv[y] = std::fabs(imgA.pixels_f32[ia+2] - imgB.pixels_f32[ib+2]);
                    if (rv[y] > max_v) max_v = rv[y];
                    if (gv[y] > max_v) max_v = gv[y];
                    if (bv[y] > max_v) max_v = bv[y];
                }
                for (int y = 0; y < n; ++y) {
                    rv[y] /= max_v; gv[y] /= max_v; bv[y] /= max_v;
                }
                line_max = max_v;
                line_hdr = true;
            } else if (use_u8) {
                for (int y = 0; y < n; ++y) {
                    int ia = (y * imgA.width + col) * 4;
                    int ib = (y * imgB.width + col) * 4;
                    rv[y] = std::abs((int)imgA.pixels[ia+0] - (int)imgB.pixels[ib+0]) / 255.0f;
                    gv[y] = std::abs((int)imgA.pixels[ia+1] - (int)imgB.pixels[ib+1]) / 255.0f;
                    bv[y] = std::abs((int)imgA.pixels[ia+2] - (int)imgB.pixels[ib+2]) / 255.0f;
                }
            }

            if (use_hdr || use_u8) {
                draw_line_section(label, rv.data(), gv.data(), bv.data(), n, chart_w, chart_h, line_max, line_hdr);
                ImGui::Spacing();
            }
        }
    }

    if (!any) {
        ImGui::TextDisabled("(no image loaded)");
    }

    ImGui::End();
}

// ─── render_stats_window ──────────────────────────────────────────────────────

namespace {

static void draw_stats_table(const char* header_label, ImU32 header_color,
                             const ImageStats& st, bool is_hdr,
                             const DiffExtraStats* extra = nullptr)
{
    ImGui::TextColored(ImColor(header_color), "%s", header_label);
    ImGui::Spacing();

    static const ImU32 ch_colors[3] = {
        IM_COL32(255, 100, 100, 255),
        IM_COL32(100, 255, 100, 255),
        IM_COL32(100, 130, 255, 255)
    };
    static const char* ch_names[3] = { "Red (R)", "Green (G)", "Blue (B)" };

    if (!ImGui::BeginTable("##stats_tbl", 5,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg   |
            ImGuiTableFlags_SizingStretchProp))
        return;

    ImGui::TableSetupColumn("Metric",        ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn(ch_names[0],     ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(ch_names[1],     ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(ch_names[2],     ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Description",   ImGuiTableColumnFlags_WidthStretch, 4.0f);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Metric");
    for (int c = 0; c < 3; ++c) {
        ImGui::TableSetColumnIndex(c + 1);
        ImGui::TextColored(ImColor(ch_colors[c]), "%s", ch_names[c]);
    }
    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted("Description");

    char buf[3][64];
    auto row_vals = [&](const char* metric, const char* desc) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(metric);
        for (int c = 0; c < 3; ++c) {
            ImGui::TableSetColumnIndex(c + 1);
            ImGui::TextColored(ImColor(ch_colors[c]), "%s", buf[c]);
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::TextWrapped("%s", desc);
    };

    // Pixels
    {
        std::string sv = fmt_comma(st.ch[0].count);
        for (int c = 0; c < 3; ++c) std::snprintf(buf[c], 64, "%s", sv.c_str());
        row_vals("Pixels",
            "Total number of pixels in the image (width x height)");
    }
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.0f", st.ch[c].min_val);
    row_vals("Min",
        "Smallest pixel value in the channel");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.0f", st.ch[c].max_val);
    row_vals("Max",
        "Largest pixel value in the channel");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.0f", st.ch[c].dynamic_range);
    row_vals("Dynamic Range",
        "Difference between max and min values (Max - Min)");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", st.ch[c].mean);
    row_vals("Mean",
        "Average pixel value: sum of all values divided by pixel count");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", st.ch[c].std_dev);
    row_vals("Std Dev",
        "Standard deviation: measures the spread of values around the mean");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", st.ch[c].variance);
    row_vals("Variance",
        "Square of standard deviation: average squared deviation from the mean");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.0f", st.ch[c].median);
    row_vals("Median",
        "Middle value when all pixels are sorted; more robust to outliers than mean");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, "%.3f", st.ch[c].skewness);
    row_vals("Skewness",
        "Asymmetry of the distribution: 0=symmetric, positive=right tail, negative=left tail");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, "%.3f", st.ch[c].kurtosis);
    row_vals("Kurtosis (excess)",
        "Tail heaviness vs. normal distribution: 0=normal, positive=heavy tails, negative=light tails");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", st.ch[c].energy);
    row_vals("Energy",
        "Mean of squared pixel values (second moment about zero)");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", st.ch[c].rms);
    row_vals("RMS",
        "Root Mean Square: square root of energy; represents overall signal magnitude");
    for (int c = 0; c < 3; ++c)
        std::snprintf(buf[c], 64, "%.3f", st.ch[c].entropy);
    row_vals("Entropy (bits)",
        "Shannon entropy: information content in bits; higher values indicate more complex/random data");

    if (extra) {
        for (int c = 0; c < 3; ++c)
            std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", extra->mse[c]);
        row_vals("MSE",
            "Mean Squared Error: average of squared differences per pixel; lower = more similar");
        for (int c = 0; c < 3; ++c)
            std::snprintf(buf[c], 64, "%.2f dB", extra->psnr[c]);
        row_vals("PSNR",
            "Peak Signal-to-Noise Ratio in dB; higher = better quality (typical range: 30-50 dB)");
        for (int c = 0; c < 3; ++c)
            std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.2f", extra->mae[c]);
        row_vals("MAE",
            "Mean Absolute Error: average of absolute differences per pixel; lower = more similar");
        for (int c = 0; c < 3; ++c)
            std::snprintf(buf[c], 64, is_hdr ? "%.4f" : "%.0f", extra->max_error[c]);
        row_vals("Max Error",
            "Largest single-pixel absolute difference between image A and image B");
    }

    ImGui::EndTable();
}

} // anonymous namespace

void render_stats_window(AppState& state) {
    if (!state.show_stats) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.85f),
            ImGuiCond_Appearing);
    }

    if (state.font_large) ImGui::PushFont(state.font_large);

    if (!ImGui::Begin("Image Statistics", &state.show_stats)) {
        ImGui::End();
        if (state.font_large) ImGui::PopFont();
        return;
    }

    bool any = false;

    // Image A
    if (state.images[0].loaded) {
        any = true;
        const ImageEntry& img = state.images[0];
        const char* fname = img.path.empty() ? "(loaded)" : basename_of(img.path.c_str());
        char header[256];
        std::snprintf(header, sizeof(header), "A: %s  (%d x %d)",
                      fname, img.width, img.height);
        ImageStats st = compute_image_stats(img);
        if (st.valid) {
            draw_stats_table(header, IM_COL32(255, 80, 255, 255), st, !img.pixels_f32.empty());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // Image B
    if (state.images[1].loaded) {
        any = true;
        const ImageEntry& img = state.images[1];
        const char* fname = img.path.empty() ? "(loaded)" : basename_of(img.path.c_str());
        char header[256];
        std::snprintf(header, sizeof(header), "B: %s  (%d x %d)",
                      fname, img.width, img.height);
        ImageStats st = compute_image_stats(img);
        if (st.valid) {
            draw_stats_table(header, IM_COL32(255, 255, 0, 255), st, !img.pixels_f32.empty());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // Diff |A-B|
    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    bool diff_on     = (state.diff.mode != DiffState::Mode::None);
    if (both_loaded && diff_on) {
        any = true;
        bool use_f32 = !state.images[0].pixels_f32.empty() && !state.images[1].pixels_f32.empty();
        DiffExtraStats extra;
        ImageStats st = compute_diff_stats(state.images[0], state.images[1], extra);
        if (st.valid)
            draw_stats_table("Diff |A-B|", IM_COL32(0, 255, 255, 255), st, use_f32, &extra);
    }

    if (!any)
        ImGui::TextDisabled("(no image loaded)");

    ImGui::End();
    if (state.font_large) ImGui::PopFont();
}

// ─── render_roi_stats_window ──────────────────────────────────────────────────

void render_roi_stats_window(AppState& state)
{
    if (!state.show_roi_stats) return;

    const auto& roi = state.roi;

    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (state.font_large) ImGui::PushFont(state.font_large);

    bool open = true;
    char title[64];
    if (roi.has_roi)
        std::snprintf(title, sizeof(title), "ROI Statistics  [%d,%d  %dx%d]",
                      roi.x, roi.y, roi.w, roi.h);
    else
        std::snprintf(title, sizeof(title), "ROI Statistics  (no ROI selected)");

    if (!ImGui::Begin(title, &open)) {
        ImGui::End();
        if (state.font_large) ImGui::PopFont();
        if (!open) state.show_roi_stats = false;
        return;
    }

    if (!roi.has_roi) {
        ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Ctrl+E 로 ROI 모드 ON → 패널 위에서 좌클릭 드래그");
        ImGui::End();
        if (state.font_large) ImGui::PopFont();
        if (!open) state.show_roi_stats = false;
        return;
    }

    // ROI 정보
    ImGui::Text("ROI: (%d, %d)  size %d x %d  = %s px",
        roi.x, roi.y, roi.w, roi.h,
        fmt_comma((int64_t)roi.w * roi.h).c_str());
    ImGui::Separator();

    bool any = false;
    for (int i = 0; i < 2; ++i) {
        const ImageEntry& img = state.images[i];
        if (!img.loaded) continue;
        any = true;
        bool use_f32 = !img.pixels_f32.empty();
        ImageStats st = compute_roi_stats(img, roi.x, roi.y, roi.w, roi.h);
        if (st.valid) {
            ImU32 col = (i == 0) ? IM_COL32(255, 220, 50, 255) : IM_COL32(50, 220, 255, 255);
            const char* lbl = (i == 0) ? "Image A (ROI)" : "Image B (ROI)";
            draw_stats_table(lbl, col, st, use_f32);
            ImGui::Spacing();
        }
    }

    bool both = state.images[0].loaded && state.images[1].loaded;
    if (both) {
        any = true;
        bool use_f32 = !state.images[0].pixels_f32.empty() && !state.images[1].pixels_f32.empty();
        DiffExtraStats extra;
        ImageStats st = compute_roi_diff_stats(state.images[0], state.images[1],
                                                roi.x, roi.y, roi.w, roi.h, extra);
        if (st.valid) {
            draw_stats_table("ROI Diff |A-B|", IM_COL32(0, 255, 200, 255), st, use_f32, &extra);
        }
    }

    if (!any)
        ImGui::TextDisabled("(no image loaded)");

    ImGui::End();
    if (state.font_large) ImGui::PopFont();
    if (!open) state.show_roi_stats = false;
}

// ─── render_scatter_plot_window ───────────────────────────────────────────────

void render_scatter_plot_window(AppState& state)
{
    if (!state.show_scatter_plot) return;
    if (!state.images[0].loaded || !state.images[1].loaded) return;

    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(vp->WorkSize.x * 0.5f, vp->WorkSize.y * 0.7f),
            ImGuiCond_Appearing);
    }

    bool open = true;
    if (!ImGui::Begin("Scatter Plot  (A vs B)", &open)) {
        ImGui::End();
        if (!open) state.show_scatter_plot = false;
        return;
    }

    // 채널 선택 UI
    static int ch_sel = 0;  // 0=R, 1=G, 2=B
    ImGui::Text("Channel:");
    ImGui::SameLine();
    if (ImGui::RadioButton("R##sp", ch_sel == 0)) ch_sel = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("G##sp", ch_sel == 1)) ch_sel = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("B##sp", ch_sel == 2)) ch_sel = 2;

    ImGui::SameLine();
    ImGui::TextDisabled("|  X=A, Y=B");

    // 데이터 추출 (캐시)
    static ScatterPlotData sp_data;
    static int last_imgA_w = -1, last_imgA_h = -1;
    static int last_imgB_w = -1, last_imgB_h = -1;

    bool needs_update = (last_imgA_w != state.images[0].width ||
                         last_imgA_h != state.images[0].height ||
                         last_imgB_w != state.images[1].width ||
                         last_imgB_h != state.images[1].height);

    if (needs_update || !sp_data.valid) {
        sp_data = extract_scatter_plot(state.images[0], state.images[1], 20000);
        last_imgA_w = state.images[0].width;
        last_imgA_h = state.images[0].height;
        last_imgB_w = state.images[1].width;
        last_imgB_h = state.images[1].height;
    }

    if (!sp_data.valid || sp_data.sampled == 0) {
        ImGui::TextDisabled("(데이터 없음)");
        ImGui::End();
        if (!open) state.show_scatter_plot = false;
        return;
    }

    // 채널 데이터 포인터 선택
    const float* ax = (ch_sel == 0) ? sp_data.r_a.data() :
                      (ch_sel == 1) ? sp_data.g_a.data() : sp_data.b_a.data();
    const float* ay = (ch_sel == 0) ? sp_data.r_b.data() :
                      (ch_sel == 1) ? sp_data.g_b.data() : sp_data.b_b.data();
    int n = sp_data.sampled;

    // 샘플 정보
    ImGui::Text("Total: %s px  /  Sampled: %s",
        fmt_comma(sp_data.total_pixels).c_str(),
        fmt_comma(sp_data.sampled).c_str());

    ImGui::Separator();

    // 차트 영역 계산
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float chart_sz = std::min(avail.x, avail.y) - 20.0f;
    if (chart_sz < 80.0f) chart_sz = 80.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    float ox = origin.x + 40.0f;  // x축 레이블 공간
    float oy = origin.y;
    float cw = chart_sz - 40.0f;
    float ch_h = chart_sz - 20.0f;

    // 차트 배경
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_chart_bg(dl, ImVec2(ox, oy), cw, ch_h);

    // 대각선 (perfect match line)
    dl->AddLine(ImVec2(ox, oy + ch_h), ImVec2(ox + cw, oy),
                IM_COL32(80, 80, 80, 200), 1.0f);

    // 점 그리기
    ImU32 dot_col[3] = {
        IM_COL32(255, 80, 80, 120),
        IM_COL32(80, 255, 80, 120),
        IM_COL32(80, 130, 255, 120)
    };
    ImU32 dot_c = dot_col[ch_sel];

    // 성능을 위해 화면 해상도 기반으로 점 크기 결정
    float dot_r = (n > 5000) ? 1.0f : 1.5f;

    for (int i = 0; i < n; ++i) {
        float px = ox + std::clamp(ax[i], 0.0f, 1.0f) * cw;
        float py = oy + ch_h * (1.0f - std::clamp(ay[i], 0.0f, 1.0f));
        dl->AddCircleFilled(ImVec2(px, py), dot_r, dot_c);
    }

    // 축 레이블
    static const char* ch_names_sp[3] = {"R", "G", "B"};
    char x_lbl[32], y_lbl[32];
    std::snprintf(x_lbl, sizeof(x_lbl), "A (%s)", ch_names_sp[ch_sel]);
    std::snprintf(y_lbl, sizeof(y_lbl), "B (%s)", ch_names_sp[ch_sel]);

    dl->AddText(ImVec2(ox + cw * 0.5f - 15.0f, oy + ch_h + 4.0f),
                IM_COL32(200, 200, 200, 200), x_lbl);
    // Y축 레이블 (간단히 텍스트로)
    dl->AddText(ImVec2(origin.x, oy + ch_h * 0.5f - 6.0f),
                IM_COL32(200, 200, 200, 200), y_lbl);

    // 0/1 눈금 표시
    ImU32 tick_col = IM_COL32(120, 120, 120, 180);
    char tick[8];
    // X축
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float px = ox + t * cw;
        dl->AddLine(ImVec2(px, oy + ch_h), ImVec2(px, oy + ch_h + 4), tick_col);
        std::snprintf(tick, sizeof(tick), "%.2f", t);
        dl->AddText(ImVec2(px - 10.0f, oy + ch_h + 6.0f),
                    IM_COL32(150, 150, 150, 200), tick);
    }
    // Y축
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float py = oy + ch_h * (1.0f - t);
        dl->AddLine(ImVec2(ox - 4, py), ImVec2(ox, py), tick_col);
        std::snprintf(tick, sizeof(tick), "%.2f", t);
        dl->AddText(ImVec2(ox - 32.0f, py - 6.0f),
                    IM_COL32(150, 150, 150, 200), tick);
    }

    // ImGui가 이 영역을 처리할 수 있도록 더미 커서 이동
    ImGui::Dummy(ImVec2(chart_sz, chart_sz));

    ImGui::End();
    if (!open) state.show_scatter_plot = false;
}
