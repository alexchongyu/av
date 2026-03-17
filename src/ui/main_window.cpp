#include "main_window.h"
#include "image_panel.h"
#include "statusbar.h"
#include "chart_windows.h"
#include "../image_loader.h"
#include "../viewport.h"
#include "../image_save.h"
#include "../image_open.h"
#include "../chart_export.h"
#include "../path_utils.h"
#include "../render_backend.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <iostream>
#include <cstring>
#include <cmath>
#include <cstdio>

// ─── Pixel format helper for balloon ──────────────────────────────────────────

static void fmt_balloon_pixel(char* buf, size_t sz, const char* prefix, int val, PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::Hex0x: std::snprintf(buf, sz, "%s0x%02X", prefix, val); break;
    case PixelFormat::HexH:  std::snprintf(buf, sz, "%s%02Xh", prefix, val);  break;
    default:                 std::snprintf(buf, sz, "%s%d", prefix, val);     break;
    }
}

// ─── Save window helpers ──────────────────────────────────────────────────────

namespace {

static const char* title_diff_label(DiffState::Mode m) {
    switch (m) {
    case DiffState::Mode::PixelAbsolute: return "Abs";
    case DiffState::Mode::PixelRelative: return "Rel";
    case DiffState::Mode::Highlight:     return "Highlight";
    case DiffState::Mode::FalseColor:    return "FalseColor";
    case DiffState::Mode::SSIM:          return "SSIM";
    default: return nullptr;
    }
}

static std::string sv_path_dir(const std::string& p) {
    auto pos = path_last_sep(p);
    return (pos == std::string::npos) ? "./" : p.substr(0, pos + 1);
}

static std::string sv_path_stem(const std::string& p) {
    std::string f = p;
    auto s = path_last_sep(p); if (s != std::string::npos) f = p.substr(s + 1);
    auto d = f.rfind('.'); if (d != std::string::npos) f = f.substr(0, d);
    return f.empty() ? "file" : f;
}

static void sv_set_ext(char* buf, const char* new_ext) {
    std::string s = buf;
    auto dot = s.rfind('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    s += new_ext;
    std::strncpy(buf, s.c_str(), 511);
    buf[511] = '\0';
}

// ─── Image Save window ────────────────────────────────────────────────────────

static void render_image_save_window(AppState& state) {
    auto& sd = state.image_save;

    // Determine extension
    auto img_ext = [&]() -> const char* {
        switch (sd.format) {
        case ImageSaveDialog::Format::BMP: return ".bmp";
        case ImageSaveDialog::Format::PPM: return ".ppm";
        default:                           return ".png";
        }
    };

    // Initialize paths on first open
    if (!sd.initialized) {
        const char* ext = img_ext();
        // Image A
        if (state.images[0].loaded && !state.images[0].path.empty()) {
            std::string p = sv_path_dir(state.images[0].path) +
                            sv_path_stem(state.images[0].path) + "_save" + ext;
            std::strncpy(sd.items[0].path, p.c_str(), 511);
        } else {
            std::snprintf(sd.items[0].path, sizeof(sd.items[0].path), "./image_a_save%s", ext);
        }
        // Image B
        if (state.images[1].loaded && !state.images[1].path.empty()) {
            std::string p = sv_path_dir(state.images[1].path) +
                            sv_path_stem(state.images[1].path) + "_save" + ext;
            std::strncpy(sd.items[1].path, p.c_str(), 511);
        } else {
            std::snprintf(sd.items[1].path, sizeof(sd.items[1].path), "./image_b_save%s", ext);
        }
        // Diff
        {
            std::string dir  = "./";
            std::string sa   = "image_a";
            std::string sb   = "image_b";
            if (state.images[0].loaded && !state.images[0].path.empty()) {
                dir = sv_path_dir(state.images[0].path);
                sa  = sv_path_stem(state.images[0].path);
            }
            if (state.images[1].loaded && !state.images[1].path.empty())
                sb = sv_path_stem(state.images[1].path);
            std::string p = dir + sa + "_vs_" + sb + "_diff" + ext;
            std::strncpy(sd.items[2].path, p.c_str(), 511);
        }
        sd.prev_format = sd.format;
        sd.initialized = true;
    }

    // Update extensions when format changes
    if (sd.prev_format != sd.format) {
        const char* ext = img_ext();
        for (auto& item : sd.items) sv_set_ext(item.path, ext);
        sd.prev_format = sd.format;
    }

    ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (state.font_medium) ImGui::PushFont(state.font_medium);
    if (!ImGui::Begin("Save Images", &open)) {
        ImGui::End();
        if (state.font_medium) ImGui::PopFont();
        if (!open) { state.show_save_dialog = false; sd.initialized = false; }
        return;
    }

    // Format selection
    ImGui::Text("Format:");
    ImGui::SameLine();
    if (ImGui::RadioButton("PNG", sd.format == ImageSaveDialog::Format::PNG))
        sd.format = ImageSaveDialog::Format::PNG;
    ImGui::SameLine();
    if (ImGui::RadioButton("BMP", sd.format == ImageSaveDialog::Format::BMP))
        sd.format = ImageSaveDialog::Format::BMP;
    ImGui::SameLine();
    if (ImGui::RadioButton("PPM", sd.format == ImageSaveDialog::Format::PPM))
        sd.format = ImageSaveDialog::Format::PPM;

    if (sd.format == ImageSaveDialog::Format::PPM) {
        bool is_bin = (sd.ppm_mode == ImageSaveDialog::PpmMode::Binary);
        bool is_asc = (sd.ppm_mode == ImageSaveDialog::PpmMode::ASCII);
        ImGui::Text("Mode:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Binary (P6)", is_bin)) sd.ppm_mode = ImageSaveDialog::PpmMode::Binary;
        ImGui::SameLine();
        if (ImGui::RadioButton("ASCII (P3)",  is_asc)) sd.ppm_mode = ImageSaveDialog::PpmMode::ASCII;
        ImGui::Text("Bits:");
        ImGui::SameLine();
        if (ImGui::RadioButton(" 8",  sd.ppm_bits ==  8)) sd.ppm_bits =  8;
        ImGui::SameLine();
        if (ImGui::RadioButton("10",  sd.ppm_bits == 10)) sd.ppm_bits = 10;
        ImGui::SameLine();
        if (ImGui::RadioButton("12",  sd.ppm_bits == 12)) sd.ppm_bits = 12;
        ImGui::SameLine();
        if (ImGui::RadioButton("16",  sd.ppm_bits == 16)) sd.ppm_bits = 16;
    }

    ImGui::Separator();

    const char* item_labels[3] = {"Image A", "Image B", "Diff"};
    for (int i = 0; i < 3; ++i) {
        auto& item = sd.items[i];
        bool loaded = (i < 2) ? state.images[i].loaded
                               : (state.images[0].loaded && state.images[1].loaded &&
                                  state.diff.mode != DiffState::Mode::None);

        char chk_id[16], inp_id[16], btn_id[24];
        std::snprintf(chk_id, sizeof(chk_id), "##ck%d", i);
        std::snprintf(inp_id, sizeof(inp_id), "##pi%d", i);
        std::snprintf(btn_id, sizeof(btn_id), "Save##si%d", i);

        if (!loaded) ImGui::BeginDisabled();
        ImGui::Checkbox(chk_id, &item.checked);
        ImGui::SameLine();
        ImGui::Text("%s", item_labels[i]);
        ImGui::SetNextItemWidth(-85.0f);
        ImGui::InputText(inp_id, item.path, sizeof(item.path));
        ImGui::SameLine();
        if (ImGui::Button(btn_id) && item.path[0] != '\0') {
            ImageSaveDialog::Target tgt =
                (i == 0) ? ImageSaveDialog::Target::ImageA :
                (i == 1) ? ImageSaveDialog::Target::ImageB :
                           ImageSaveDialog::Target::Diff;
            perform_save(item.path, tgt, state);
        }
        if (!loaded) ImGui::EndDisabled();
    }

    ImGui::Separator();

    // Status message
    if (!sd.status_msg.empty()) {
        if (sd.status_error)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
    }

    // Save All Checked + Close
    if (ImGui::Button("Save All Checked")) {
        sd.status_msg.clear(); sd.status_error = false;
        for (int i = 0; i < 3; ++i) {
            auto& item = sd.items[i];
            if (!item.checked || item.path[0] == '\0') continue;
            bool loaded = (i < 2) ? state.images[i].loaded
                                   : (state.images[0].loaded && state.images[1].loaded &&
                                      state.diff.mode != DiffState::Mode::None);
            if (!loaded) continue;
            ImageSaveDialog::Target tgt =
                (i == 0) ? ImageSaveDialog::Target::ImageA :
                (i == 1) ? ImageSaveDialog::Target::ImageB :
                           ImageSaveDialog::Target::Diff;
            perform_save(item.path, tgt, state);
            if (sd.status_error) break;
        }
    }

    ImGui::SameLine();
    {
        float close_w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float x = ImGui::GetWindowWidth() - close_w - ImGui::GetStyle().WindowPadding.x;
        if (ImGui::GetCursorPosX() < x) ImGui::SetCursorPosX(x);
    }
    if (ImGui::Button("Close")) {
        state.show_save_dialog = false;
        sd.initialized = false;
    }

    ImGui::End();
    if (state.font_medium) ImGui::PopFont();
    if (!open) { state.show_save_dialog = false; sd.initialized = false; }
}

// ─── Chart Save window (Histogram / H-Line Cut / V-Line Cut) ─────────────────

static void render_chart_save_window(AppState& state, bool is_histogram) {
    auto& sd = state.chart_save;

    bool is_hcut = !is_histogram && state.show_hline_cut;
    // is_vcut = !is_histogram && !is_hcut

    const char* title   = is_histogram ? "Save Histogram"
                        : is_hcut      ? "Save H-Line Cut"
                                       : "Save V-Line Cut";
    const char* suffix  = is_histogram ? "_histogram"
                        : is_hcut      ? "_hcut"
                                       : "_vcut";

    // Re-initialize if not done or chart type changed
    if (!sd.initialized || sd.init_was_histogram != is_histogram || sd.init_was_hcut != is_hcut) {
        std::string dir_a = "./", sa = "image_a";
        std::string dir_b = "./", sb = "image_b";
        if (state.images[0].loaded && !state.images[0].path.empty()) {
            dir_a = sv_path_dir(state.images[0].path);
            sa    = sv_path_stem(state.images[0].path);
        }
        if (state.images[1].loaded && !state.images[1].path.empty()) {
            dir_b = sv_path_dir(state.images[1].path);
            sb    = sv_path_stem(state.images[1].path);
        }

        // PNG items
        { std::string p = dir_a + sa + suffix + ".png"; std::strncpy(sd.png_items[0].path, p.c_str(), 511); }
        { std::string p = dir_b + sb + suffix + ".png"; std::strncpy(sd.png_items[1].path, p.c_str(), 511); }
        { std::string p = dir_a + sa + "_vs_" + sb + suffix + "_diff.png"; std::strncpy(sd.png_items[2].path, p.c_str(), 511); }

        // CSV items
        { std::string p = dir_a + sa + suffix + ".csv"; std::strncpy(sd.csv_items[0].path, p.c_str(), 511); }
        { std::string p = dir_b + sb + suffix + ".csv"; std::strncpy(sd.csv_items[1].path, p.c_str(), 511); }
        { std::string p = dir_a + sa + "_vs_" + sb + suffix + "_diff.csv"; std::strncpy(sd.csv_items[2].path, p.c_str(), 511); }

        sd.init_was_histogram = is_histogram;
        sd.init_was_hcut      = is_hcut;
        sd.initialized        = true;
    }

    ImGui::SetNextWindowSize(ImVec2(610, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (state.font_medium) ImGui::PushFont(state.font_medium);
    if (!ImGui::Begin(title, &open)) {
        ImGui::End();
        if (state.font_medium) ImGui::PopFont();
        if (!open) { state.show_save_dialog = false; sd.initialized = false; }
        return;
    }

    // Resolution
    ImGui::Text("Resolution:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##cew", &sd.export_width, 0);
    ImGui::SameLine(); ImGui::Text("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##ceh", &sd.export_height, 0);
    sd.export_width  = std::max(sd.export_width,  320);
    sd.export_height = std::max(sd.export_height, 240);

    // Channel mode
    ImGui::Text("Channels:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Combined##cc",     !sd.separate_channels)) sd.separate_channels = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Separate R/G/B##cs", sd.separate_channels)) sd.separate_channels = true;

    ImGui::Separator();

    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    const char* item_labels[3] = {"Image A", "Image B", "Diff"};

    // Helper: do a single export and update status
    auto do_png = [&](int i, const char* path_buf) {
        if (path_buf[0] == '\0') return;
        bool ok = false;
        int ew = sd.export_width, eh = sd.export_height;
        bool sep = sd.separate_channels;
        if (is_histogram) {
            HistogramData hd = (i == 2) ? extract_diff_histogram(state.images[0], state.images[1])
                                        : extract_histogram(state.images[i]);
            ok = export_histogram_png(hd, path_buf, ew, eh, sep);
        } else if (is_hcut) {
            LineCutData ld = (i == 2) ? extract_diff_hline_cut(state.images[0], state.images[1], state.views[0])
                                      : extract_hline_cut(state.images[i], state.views[i < 2 ? i : 0]);
            ok = export_linecut_png(ld, path_buf, ew, eh, sep, "Pixel Column");
        } else {
            LineCutData ld = (i == 2) ? extract_diff_vline_cut(state.images[0], state.images[1], state.views[0])
                                      : extract_vline_cut(state.images[i], state.views[i < 2 ? i : 0]);
            ok = export_linecut_png(ld, path_buf, ew, eh, sep, "Pixel Row");
        }
        if (!ok) {
            sd.status_error = true;
            sd.status_msg   = "Error: Failed to write " + std::string(path_buf);
        } else {
            sd.status_error = false;
            auto sl = path_last_sep(std::string(path_buf));
            sd.status_msg = "Saved: " + std::string(path_buf).substr(sl != std::string::npos ? sl + 1 : 0);
        }
    };
    auto do_csv = [&](int i, const char* path_buf) {
        if (path_buf[0] == '\0') return;
        bool ok = false;
        if (is_histogram) {
            HistogramData hd = (i == 2) ? extract_diff_histogram(state.images[0], state.images[1])
                                        : extract_histogram(state.images[i]);
            ok = export_histogram_csv(hd, path_buf);
        } else if (is_hcut) {
            LineCutData ld = (i == 2) ? extract_diff_hline_cut(state.images[0], state.images[1], state.views[0])
                                      : extract_hline_cut(state.images[i], state.views[i < 2 ? i : 0]);
            ok = export_linecut_csv(ld, path_buf);
        } else {
            LineCutData ld = (i == 2) ? extract_diff_vline_cut(state.images[0], state.images[1], state.views[0])
                                      : extract_vline_cut(state.images[i], state.views[i < 2 ? i : 0]);
            ok = export_linecut_csv(ld, path_buf);
        }
        if (!ok) {
            sd.status_error = true;
            sd.status_msg   = "Error: Failed to write " + std::string(path_buf);
        } else {
            sd.status_error = false;
            auto sl = path_last_sep(std::string(path_buf));
            sd.status_msg = "Saved: " + std::string(path_buf).substr(sl != std::string::npos ? sl + 1 : 0);
        }
    };

    for (int i = 0; i < 3; ++i) {
        auto& pi = sd.png_items[i];
        auto& ci = sd.csv_items[i];
        bool loaded = (i < 2) ? state.images[i].loaded : both_loaded;

        char chk_id[16];
        std::snprintf(chk_id, sizeof(chk_id), "##cck%d", i);

        if (!loaded) ImGui::BeginDisabled();
        ImGui::Checkbox(chk_id, &pi.checked);
        ImGui::SameLine();
        ImGui::Text("%s", item_labels[i]);

        // PNG row
        {
            char inp_id[20], btn_id[24];
            std::snprintf(inp_id, sizeof(inp_id), "##cpng%d", i);
            std::snprintf(btn_id, sizeof(btn_id), "Save##cpng%d", i);
            ImGui::Text("  PNG:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-85.0f);
            ImGui::InputText(inp_id, pi.path, sizeof(pi.path));
            ImGui::SameLine();
            if (ImGui::Button(btn_id)) do_png(i, pi.path);
        }
        // CSV row
        {
            char inp_id[20], btn_id[24];
            std::snprintf(inp_id, sizeof(inp_id), "##ccsv%d", i);
            std::snprintf(btn_id, sizeof(btn_id), "Save##ccsv%d", i);
            ImGui::Text("  CSV:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-85.0f);
            ImGui::InputText(inp_id, ci.path, sizeof(ci.path));
            ImGui::SameLine();
            if (ImGui::Button(btn_id)) do_csv(i, ci.path);
        }
        if (!loaded) ImGui::EndDisabled();
    }

    // Status message
    if (!sd.status_msg.empty()) {
        ImGui::Separator();
        if (sd.status_error)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
    }

    ImGui::Separator();

    // Save All Checked
    if (ImGui::Button("Save All Checked")) {
        sd.status_msg.clear(); sd.status_error = false;
        for (int i = 0; i < 3 && !sd.status_error; ++i) {
            bool loaded = (i < 2) ? state.images[i].loaded : both_loaded;
            if (!loaded || !sd.png_items[i].checked) continue;
            if (sd.png_items[i].path[0] != '\0') do_png(i, sd.png_items[i].path);
            if (!sd.status_error && sd.csv_items[i].path[0] != '\0') do_csv(i, sd.csv_items[i].path);
        }
        if (!sd.status_error) sd.status_msg = "All checked items saved.";
    }

    ImGui::SameLine();
    {
        float close_w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float x = ImGui::GetWindowWidth() - close_w - ImGui::GetStyle().WindowPadding.x;
        if (ImGui::GetCursorPosX() < x) ImGui::SetCursorPosX(x);
    }
    if (ImGui::Button("Close")) {
        state.show_save_dialog = false;
        sd.initialized = false;
    }

    ImGui::End();
    if (state.font_medium) ImGui::PopFont();
    if (!open) { state.show_save_dialog = false; sd.initialized = false; }
}

// ─── Stats Save window ────────────────────────────────────────────────────────

static void render_stats_save_window(AppState& state) {
    auto& sd = state.stats_save;

    if (!sd.initialized) {
        std::string dir_a = "./", sa = "image_a";
        std::string dir_b = "./", sb = "image_b";
        if (state.images[0].loaded && !state.images[0].path.empty()) {
            dir_a = sv_path_dir(state.images[0].path);
            sa    = sv_path_stem(state.images[0].path);
        }
        if (state.images[1].loaded && !state.images[1].path.empty()) {
            dir_b = sv_path_dir(state.images[1].path);
            sb    = sv_path_stem(state.images[1].path);
        }
        { std::string p = dir_a + sa + "_stats.csv"; std::strncpy(sd.items[0].path, p.c_str(), 511); }
        { std::string p = dir_b + sb + "_stats.csv"; std::strncpy(sd.items[1].path, p.c_str(), 511); }
        { std::string p = dir_a + sa + "_vs_" + sb + "_stats.csv"; std::strncpy(sd.items[2].path, p.c_str(), 511); }
        sd.initialized = true;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (state.font_medium) ImGui::PushFont(state.font_medium);
    if (!ImGui::Begin("Save Statistics", &open)) {
        ImGui::End();
        if (state.font_medium) ImGui::PopFont();
        if (!open) { state.show_save_dialog = false; sd.initialized = false; }
        return;
    }

    bool both_loaded = state.images[0].loaded && state.images[1].loaded;
    const char* item_labels[3] = {"Image A", "Image B", "Diff"};

    for (int i = 0; i < 3; ++i) {
        auto& item = sd.items[i];
        bool loaded = (i < 2) ? state.images[i].loaded : both_loaded;

        char chk_id[16], inp_id[16], btn_id[24];
        std::snprintf(chk_id, sizeof(chk_id), "##stck%d", i);
        std::snprintf(inp_id, sizeof(inp_id), "##stpi%d", i);
        std::snprintf(btn_id, sizeof(btn_id), "Save##stbtn%d", i);

        if (!loaded) ImGui::BeginDisabled();
        ImGui::Checkbox(chk_id, &item.checked);
        ImGui::SameLine();
        ImGui::Text("%s", item_labels[i]);
        ImGui::SetNextItemWidth(-85.0f);
        ImGui::InputText(inp_id, item.path, sizeof(item.path));
        ImGui::SameLine();
        if (ImGui::Button(btn_id) && item.path[0] != '\0') {
            bool ok = false;
            if (i == 2) {
                DiffExtraStats extra;
                ImageStats st = compute_diff_stats(state.images[0], state.images[1], extra);
                ok = export_stats_csv(st, item.path, &extra);
            } else {
                ImageStats st = compute_image_stats(state.images[i]);
                ok = export_stats_csv(st, item.path);
            }
            if (!ok) {
                sd.status_error = true;
                sd.status_msg   = "Error: Failed to write " + std::string(item.path);
            } else {
                sd.status_error = false;
                auto sl = path_last_sep(std::string(item.path));
                sd.status_msg = "Saved: " + std::string(item.path).substr(sl != std::string::npos ? sl + 1 : 0);
            }
        }
        if (!loaded) ImGui::EndDisabled();
    }

    // Status message
    if (!sd.status_msg.empty()) {
        ImGui::Separator();
        if (sd.status_error)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sd.status_msg.c_str());
    }

    ImGui::Separator();

    if (ImGui::Button("Save All Checked")) {
        sd.status_msg.clear(); sd.status_error = false;
        for (int i = 0; i < 3; ++i) {
            auto& item = sd.items[i];
            if (!item.checked || item.path[0] == '\0') continue;
            bool loaded = (i < 2) ? state.images[i].loaded : both_loaded;
            if (!loaded) continue;
            bool ok = false;
            if (i == 2) {
                DiffExtraStats extra;
                ImageStats st = compute_diff_stats(state.images[0], state.images[1], extra);
                ok = export_stats_csv(st, item.path, &extra);
            } else {
                ImageStats st = compute_image_stats(state.images[i]);
                ok = export_stats_csv(st, item.path);
            }
            if (!ok) { sd.status_error = true; sd.status_msg = "Error: " + std::string(item.path); break; }
        }
        if (!sd.status_error) sd.status_msg = "All checked items saved.";
    }

    ImGui::SameLine();
    {
        float close_w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float x = ImGui::GetWindowWidth() - close_w - ImGui::GetStyle().WindowPadding.x;
        if (ImGui::GetCursorPosX() < x) ImGui::SetCursorPosX(x);
    }
    if (ImGui::Button("Close")) {
        state.show_save_dialog = false;
        sd.initialized = false;
    }

    ImGui::End();
    if (state.font_medium) ImGui::PopFont();
    if (!open) { state.show_save_dialog = false; sd.initialized = false; }
}

} // namespace

// ─── Static panel instances ───────────────────────────────────────────────────
static ImagePanel s_panel_left;
static ImagePanel s_panel_right;
static ImagePanel s_panel_diff;
static StatusBar  s_statusbar;

// ─── SSIM computer ────────────────────────────────────────────────────────────
static SSIMComputer s_ssim_computer;
static bool          s_ssim_ready  = false;
static SSIMResult    s_ssim_result;

// ─── init ─────────────────────────────────────────────────────────────────────

bool MainWindow::init() {
    if (!diff_renderer_.init()) {
        std::cerr << "[MainWindow] DiffRenderer init failed\n";
        return false;
    }
    if (!s_panel_left.init()) {
        std::cerr << "[MainWindow] left panel init failed\n";
        return false;
    }
    if (!s_panel_right.init()) {
        std::cerr << "[MainWindow] right panel init failed\n";
        return false;
    }
    if (!s_panel_diff.init()) {
        std::cerr << "[MainWindow] diff panel init failed\n";
        return false;
    }
    inited_ = true;
    return true;
}

// ─── render_menubar ───────────────────────────────────────────────────────────

void MainWindow::render_menubar(AppState& state) {
    if (!ImGui::BeginMenuBar()) return;

    // ── File ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Image A…", "")) {
            open_open_file_dialog(state, 0);
        }
        if (ImGui::MenuItem("Open Image B…", "")) {
            open_open_file_dialog(state, 1);
        }
        if (ImGui::MenuItem("Open Image…", "Shift+Cmd+O")) {
            open_open_file_dialog(state, state.active_panel);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Images…", "Shift+Cmd+S")) {
            state.show_save_dialog = !state.show_save_dialog;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Q")) {
            state.quit = true;
        }
        ImGui::EndMenu();
    }

    // ── View ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit to Window", "0")) {
            auto& a = state.images[0]; auto& b = state.images[1];
            if (a.loaded) state.views[0].fit = true;
            if (b.loaded) state.views[1].fit = true;
        }
        if (ImGui::MenuItem("1:1 Pixel", "1")) {
            state.views[0].zoom = 1.0f;
            state.views[1].zoom = 1.0f;
            viewport_center(state.views[0]);
            viewport_center(state.views[1]);
        }
        ImGui::Separator();
        ImGui::MenuItem("Sync Viewports", "S", &state.sync_viewports);
        ImGui::Separator();
        if (ImGui::MenuItem("Pathfinder: Image", "P", state.pathfinder_mode == 1)) {
            state.pathfinder_mode = (state.pathfinder_mode == 1) ? 0 : 1;
        }
        if (ImGui::MenuItem("Pathfinder: Schematic", "Ctrl+P", state.pathfinder_mode == 2)) {
            state.pathfinder_mode = (state.pathfinder_mode == 2) ? 0 : 2;
        }
        ImGui::MenuItem("Show Image Info", "I", &state.show_info);
        ImGui::MenuItem("Show Pixel Info", "V", &state.show_pixel_info);
        ImGui::Separator();
        if (ImGui::MenuItem("Show Histogram", "Ctrl+H", state.show_histogram)) {
            state.show_histogram = !state.show_histogram;
            if (state.show_histogram) { state.show_hline_cut = false; state.show_vline_cut = false; state.show_stats = false; }
        }
        if (ImGui::MenuItem("Show H-Line Cut", "Ctrl+L", state.show_hline_cut)) {
            state.show_hline_cut = !state.show_hline_cut;
            if (state.show_hline_cut) { state.show_histogram = false; state.show_vline_cut = false; state.show_stats = false; }
        }
        if (ImGui::MenuItem("Show V-Line Cut", "Ctrl+Y", state.show_vline_cut)) {
            state.show_vline_cut = !state.show_vline_cut;
            if (state.show_vline_cut) { state.show_histogram = false; state.show_hline_cut = false; state.show_stats = false; }
        }
        if (ImGui::MenuItem("Show Statistics", "Ctrl+S", state.show_stats)) {
            state.show_stats = !state.show_stats;
            if (state.show_stats) { state.show_histogram = false; state.show_hline_cut = false; state.show_vline_cut = false; }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("ROI Stats", "Ctrl+E", state.show_roi_stats)) {
            state.roi.active = !state.roi.active;
            state.show_roi_stats = state.roi.active;
            if (!state.roi.active) { state.roi.has_roi = false; }
        }
        if (ImGui::MenuItem("Scatter Plot", "Ctrl+T", state.show_scatter_plot)) {
            state.show_scatter_plot = !state.show_scatter_plot;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Overlay/Blend", "O", state.overlay.active)) {
            state.overlay.active = !state.overlay.active;
        }
        if (state.overlay.active) {
            ImGui::PushItemWidth(150.0f);
            ImGui::SliderFloat("##blend_alpha", &state.overlay.alpha, 0.0f, 1.0f, "Blend: %.2f");
            ImGui::PopItemWidth();
            bool is_curtain = (state.overlay.mode == OverlayState::Mode::Curtain);
            if (ImGui::MenuItem("  Blend mode",   nullptr, !is_curtain)) state.overlay.mode = OverlayState::Mode::Blend;
            if (ImGui::MenuItem("  Curtain mode", nullptr,  is_curtain)) state.overlay.mode = OverlayState::Mode::Curtain;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show Borders", "B", state.show_borders)) {
            state.show_borders = !state.show_borders;
        }
        if (ImGui::BeginMenu("Pixel Format")) {
            if (ImGui::MenuItem("Decimal (128)", "Ctrl+X", state.pixel_format == PixelFormat::Decimal))
                state.pixel_format = PixelFormat::Decimal;
            if (ImGui::MenuItem("Hex (0x80)", nullptr, state.pixel_format == PixelFormat::Hex0x))
                state.pixel_format = PixelFormat::Hex0x;
            if (ImGui::MenuItem("Hex (80h)", nullptr, state.pixel_format == PixelFormat::HexH))
                state.pixel_format = PixelFormat::HexH;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Hotkey Reference", "Ctrl+Shift+H", state.show_hotkey_help)) {
            state.show_hotkey_help = !state.show_hotkey_help;
        }
        ImGui::EndMenu();
    }

    // ── Diff ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("Diff")) {
        bool none = state.diff.mode == DiffState::Mode::None;
        bool abs_ = state.diff.mode == DiffState::Mode::PixelAbsolute;
        bool rel_ = state.diff.mode == DiffState::Mode::PixelRelative;
        bool hl_  = state.diff.mode == DiffState::Mode::Highlight;
        bool fc_  = state.diff.mode == DiffState::Mode::FalseColor;
        bool ssim = state.diff.mode == DiffState::Mode::SSIM;

        if (ImGui::MenuItem("Off",        "Ctrl+D", none)) state.diff.mode = DiffState::Mode::None;
        if (ImGui::MenuItem("Absolute",   "Ctrl+3", abs_)) state.diff.mode = DiffState::Mode::PixelAbsolute;
        if (ImGui::MenuItem("Relative",   "Ctrl+4", rel_)) state.diff.mode = DiffState::Mode::PixelRelative;
        if (ImGui::MenuItem("Highlight",  "Ctrl+5", hl_ )) state.diff.mode = DiffState::Mode::Highlight;
        if (ImGui::MenuItem("FalseColor", "Ctrl+6", fc_ )) state.diff.mode = DiffState::Mode::FalseColor;
        if (ImGui::MenuItem("SSIM",       "Ctrl+7", ssim)) state.diff.mode = DiffState::Mode::SSIM;
        ImGui::Separator();
        ImGui::SliderFloat("Amplify", &state.diff.amplify, 0.5f, 50.0f, "%.1f");
        ImGui::EndMenu();
    }

    // FPS in menu bar right side
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.0f fps  |  U: hide UI", ImGui::GetIO().Framerate);
    float fps_w = ImGui::CalcTextSize(fps).x;
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - fps_w - 8.0f);
    ImGui::TextDisabled("%s", fps);

    ImGui::EndMenuBar();
}

// ─── render_hotkey_help_window ────────────────────────────────────────────────

static void render_hotkey_help_window(AppState& state) {
    if (!state.show_hotkey_help) return;

    ImGuiIO& io = ImGui::GetIO();
    float vp_w = io.DisplaySize.x;
    float vp_h = io.DisplaySize.y;
    float win_w = std::min(vp_w * 0.52f, 660.0f);
    // Auto-height capped at 92% of viewport; positioned near top-center
    ImGui::SetNextWindowSizeConstraints(ImVec2(win_w, 0), ImVec2(win_w, vp_h * 0.92f));
    ImGui::SetNextWindowPos(ImVec2((vp_w - win_w) * 0.5f, vp_h * 0.04f), ImGuiCond_Always);

    if (state.font_medium) ImGui::PushFont(state.font_medium);

    if (!ImGui::Begin("Hotkey Reference", &state.show_hotkey_help,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        if (state.font_medium) ImGui::PopFont();
        return;
    }

    struct HotkeyEntry { const char* category; const char* shortcut; const char* description; };
    static const HotkeyEntry entries[] = {
        // Navigation
        { "Navigation", "H / Left",                "Pan left (1px)" },
        { "Navigation", "L / Right",               "Pan right (1px)" },
        { "Navigation", "K / Up",                  "Pan up (1px)" },
        { "Navigation", "J / Down",                "Pan down (1px)" },
        { "Navigation", "Shift + H,L,K,J / Arrow", "Fast pan (pan_step px)" },
        { "Navigation", "Cmd+Shift + H,L,K,J",     "Jump to edge" },
        { "Navigation", "G",                       "Center image" },
        { "Navigation", "Mouse Left Drag",         "Pan image" },
        // Zoom
        { "Zoom", "+ / = / Numpad+",               "Zoom in (x2)" },
        { "Zoom", "- / Numpad-",                   "Zoom out (/2)" },
        { "Zoom", "Z",                             "Zoom in" },
        { "Zoom", "Shift+Z",                       "Zoom out" },
        { "Zoom", "0",                             "Fit to window" },
        { "Zoom", "1~8",                           "Zoom level 2^n (1=2x, 2=4x, ... 8=256x)" },
        { "Zoom", "F",                             "Toggle fit-to-window" },
        { "Zoom", "Space",                         "1:1 zoom + center" },
        { "Zoom", "Mouse Wheel",                   "Zoom in/out" },
        { "Zoom", "Mouse Right Drag",              "Zoom to selection" },
        // Display
        { "Display", "U",                          "Toggle UI overlay (menubar/statusbar)" },
        { "Display", "I",                          "Toggle image info window" },
        { "Display", "V",                          "Toggle cursor pixel value balloon" },
        { "Display", "S",                          "Toggle viewport sync (A <-> B)" },
        { "Display", "W",                          "Toggle windowed mode (title bar)" },
        { "Display", "Tab",                        "Switch active panel (A <-> B)" },
        { "Display", "R",                          "Rotate image CW 90 degrees" },
        { "Display", "Ctrl+R",                     "Rotate image CCW 90 degrees" },
        { "Display", "Shift+Space",                "Swap A/B images" },
        { "Display", "M",                          "Toggle crosshair overlay" },
        { "Display", "P",                          "Pathfinder: image minimap" },
        { "Display", "Ctrl+P",                     "Pathfinder: schematic mode" },
        // Channel
        { "Channel", "Shift+R",                    "Show Red channel only" },
        { "Channel", "Shift+G",                    "Show Green channel only" },
        { "Channel", "Shift+B",                    "Show Blue channel only" },
        { "Channel", "Shift+C",                    "Show RGB (default)" },
        // Analysis
        { "Analysis", "Ctrl+H",                    "Toggle histogram window" },
        { "Analysis", "Ctrl+L",                    "Toggle H-Line Cut" },
        { "Analysis", "Ctrl+Y",                    "Toggle V-Line Cut" },
        { "Analysis", "Ctrl+S",                    "Toggle statistics window" },
        { "Analysis", "Ctrl+E",                    "Toggle ROI selection mode (left-drag to select)" },
        { "Analysis", "Ctrl+T",                    "Toggle Scatter Plot (A vs B pixel values)" },
        // Overlay
        { "Display", "B",                           "Toggle panel borders" },
        { "Display", "Ctrl+X",                      "Cycle pixel value format (Dec / 0xHex / Hexh)" },
        { "Overlay", "O",                          "Toggle Overlay/Blend comparison mode" },
        { "Overlay", "Curtain mode",               "Left-drag to move divider; mode in menu" },
        // Sequence
        { "Sequence", "N",                         "Next image in directory sequence" },
        { "Sequence", "Shift+N",                   "Previous image in directory sequence" },
        { "Sequence", "A",                         "Toggle slideshow auto-play" },
        { "Sequence", "Shift+Up",                  "Slideshow interval +1s" },
        { "Sequence", "Shift+Down",                "Slideshow interval -1s" },
        // Diff
        { "Diff", "Ctrl+D",                        "Disable diff mode" },
        { "Diff", "Ctrl+3",                        "Diff: Pixel Absolute" },
        { "Diff", "Ctrl+4",                        "Diff: Pixel Relative" },
        { "Diff", "Ctrl+5",                        "Diff: Highlight (red)" },
        { "Diff", "Ctrl+6",                        "Diff: False Color" },
        { "Diff", "Ctrl+7",                        "Diff: SSIM" },
        { "Diff", "[",                             "Decrease diff amplify" },
        { "Diff", "]",                             "Increase diff amplify" },
        { "Diff", "\\",                            "Reset diff amplify (1.0x)" },
        { "Diff", "Ctrl+8",                        "Toggle tolerance-based diff" },
        { "Diff", "Shift+]",                       "Increase diff threshold (+1)" },
        { "Diff", "Shift+[",                       "Decrease diff threshold (-1)" },
        { "Diff", "Ctrl+\\",                       "Reset diff threshold (0)" },
        // File
        { "File", "Shift+Ctrl+O",                  "Open image file" },
        { "File", "Shift+Ctrl+S",                  "Save dialog" },
        { "File", "Q",                             "Quit" },
        // Help
        { "Help", "Ctrl+Shift+H",                  "Toggle this hotkey reference" },
        // CLI Options
        { "CLI", "av [image_a] [image_b]",          "Open one or two images" },
        { "CLI", "--diff-mode <mode>",              "none|abs|rel|highlight|falsecolor|ssim  (default: none)" },
        { "CLI", "--zoom <factor>",                 "fit|1|2.0 etc.  (default: fit)" },
        { "CLI", "--sync / --no-sync",              "Enable/disable viewport sync  (default: on)" },
        { "CLI", "--amplify <val>",                 "Diff amplification 0.1-100  (default: 1.0)" },
        { "CLI", "--fullscreen",                    "Start in fullscreen" },
        { "CLI", "--geometry <WxH>",                "Initial window size  (default: 1280x720)" },
        { "CLI", "--profile <file>",                "ICC colour profile path" },
        { "CLI", "--no-color-mgmt",                 "Disable colour management" },
        { "CLI", "-p, --pan-step <N>",              "Shift+hjkl jump size in pixels  (default: 32)" },
        { "CLI", "-bc <A> <B> <D>",                 "Border colours for A/B/Diff as 6-digit hex" },
        { "CLI", "-nb",                              "Start with panel borders hidden" },
        { "CLI", "-d, --diff",                      "Show pixel-absolute diff (shortcut)" },
        { "CLI", "--version",                       "Print version and exit" },
        { "CLI", "-h, --help",                      "Print this help" },
    };

    constexpr ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg   |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("hotkeys", 3, tflags)) {
        ImGui::TableSetupColumn("Category",    ImGuiTableColumnFlags_WidthFixed,    90.0f);
        ImGui::TableSetupColumn("Shortcut",    ImGuiTableColumnFlags_WidthFixed,   190.0f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        const char* prev_cat = nullptr;
        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (prev_cat && strcmp(prev_cat, e.category) == 0) {
                ImGui::TextDisabled(" ");
            } else {
                ImGui::TextUnformatted(e.category);
                prev_cat = e.category;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.shortcut);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.description);
        }
        ImGui::EndTable();
    }

    ImGui::End();
    if (state.font_medium) ImGui::PopFont();
}

// ─── render ───────────────────────────────────────────────────────────────────

void MainWindow::render(AppState& state) {
    if (!inited_) return;

    // ── Dynamic window title ─────────────────────────────────────────────────
    if (state.window) {
        static std::string prev_title;
        std::string title;
        if (state.windowed_mode) {
            title = "av";
            bool has_a = state.images[0].loaded;
            bool has_b = state.images[1].loaded;
            if (has_a || has_b) {
                title += " \xe2\x80\x94 ";  // em dash
                if (has_a) title += path_basename(state.images[0].path.c_str());
                if (has_a && has_b) title += " | ";
                if (has_b) title += path_basename(state.images[1].path.c_str());
                const char* dl = title_diff_label(state.diff.mode);
                if (dl) { title += " ["; title += dl; title += "]"; }
            }
        } else {
            title = "Advanced Pixel Lens";
        }
        if (title != prev_title) {
            SDL_SetWindowTitle(state.window, title.c_str());
            prev_title = std::move(title);
        }
    }

    // ── Process pending image-open (set by SDL dialog callback) ──────────────
    if (state.open_state.open_pending && !state.open_state.opened_path.empty()) {
        int target = state.open_state.open_target;
        if (target >= 0 && target <= 1) {
            if (state.open_state.clear_other) {
                int other = 1 - target;
                if (state.images[other].loaded)
                    free_image(state.images[other]);
            }
            load_image(state.open_state.opened_path, state.images[target]);
            state.views[target].fit   = true;
            state.views[target].pan_x = 0.0f;
            state.views[target].pan_y = 0.0f;
            state.diff.psnr_computed  = false;  // invalidate PSNR cache

            // 시퀀스 스캔: 새 이미지 로드 시 디렉토리 탐색
            int cur_idx = -1;
            state.sequences[target].files = scan_image_directory(
                state.images[target].path, cur_idx);
            state.sequences[target].current_index = cur_idx;
        }
        state.open_state.opened_path.clear();
        state.open_state.open_pending = false;
    }

    // ── Process pending context-menu save (set by SDL dialog callback) ────────
    if (state.context_save.save_pending) {
        const auto& path = state.context_save.save_path;
        // Determine format from extension
        auto dot = path.rfind('.');
        std::string ext = (dot != std::string::npos) ? path.substr(dot) : "";
        if (ext == ".bmp")      state.image_save.format = ImageSaveDialog::Format::BMP;
        else if (ext == ".ppm") state.image_save.format = ImageSaveDialog::Format::PPM;
        else                    state.image_save.format = ImageSaveDialog::Format::PNG;

        // Map target_type to Target enum
        auto target = (state.context_save.save_target == 2) ? ImageSaveDialog::Target::Diff
                    : (state.context_save.save_target == 1) ? ImageSaveDialog::Target::ImageB
                    : ImageSaveDialog::Target::ImageA;
        perform_save(path, target, state);
        state.context_save.save_pending = false;
        state.context_save.save_path.clear();
    }

    // ── Upload SSIM result on main thread if ready ────────────────────────────
    if (s_ssim_ready) {
        s_ssim_ready = false;
        if (state.diff.ssim_texture_id) {
            if (is_software_mode()) {
                SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(state.diff.ssim_texture_id));
            } else {
                GLuint id = static_cast<GLuint>(state.diff.ssim_texture_id);
                glDeleteTextures(1, &id);
            }
            state.diff.ssim_texture_id = 0;
        }
        if (s_ssim_result.success && !s_ssim_result.heatmap.empty()) {
            if (is_software_mode()) {
                // Convert float heatmap to falsecolor RGBA8 for SDL texture
                int w = s_ssim_result.w, h = s_ssim_result.h;
                std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
                for (int i = 0; i < w * h; ++i) {
                    float t = std::clamp(s_ssim_result.heatmap[i], 0.0f, 1.0f);
                    // falsecolor: dark blue → blue → green → yellow → red
                    float r, g, b;
                    if      (t < 0.25f) { float s = t * 4.0f;        r = 0;    g = 0;    b = 0.5f + 0.5f*s; }
                    else if (t < 0.50f) { float s = (t-0.25f) * 4.0f; r = 0;    g = s;    b = 1.0f - s; }
                    else if (t < 0.75f) { float s = (t-0.50f) * 4.0f; r = s;    g = 1.0f; b = 0; }
                    else                { float s = (t-0.75f) * 4.0f; r = 1.0f; g = 1.0f-s; b = 0; }
                    rgba[i*4+0] = (uint8_t)(r * 255.0f);
                    rgba[i*4+1] = (uint8_t)(g * 255.0f);
                    rgba[i*4+2] = (uint8_t)(b * 255.0f);
                    rgba[i*4+3] = 255;
                }
                SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32,
                                                           rgba.data(), w * 4);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_render_ctx.sdl_renderer, surf);
                    SDL_DestroySurface(surf);
                    state.diff.ssim_texture_id = reinterpret_cast<uintptr_t>(tex);
                }
                // Store falsecolor pixels for cpu_render_image viewport transform
                state.diff.ssim_pixels = std::move(rgba);
                state.diff.ssim_w = w;
                state.diff.ssim_h = h;
            } else {
                state.diff.ssim_texture_id = gl_upload_texture_r32f(
                    s_ssim_result.heatmap.data(),
                    s_ssim_result.w,
                    s_ssim_result.h);
            }
        }
        state.diff.ssim_score     = s_ssim_result.score;
        state.diff.ssim_computing = false;
    }

    // ── Trigger SSIM computation when mode switches ───────────────────────────
    static DiffState::Mode prev_diff_mode = DiffState::Mode::None;
    if (state.diff.mode == DiffState::Mode::SSIM &&
        prev_diff_mode != DiffState::Mode::SSIM &&
        state.images[0].loaded && state.images[1].loaded) {
        state.diff.ssim_computing = true;
        state.diff.ssim_score     = -1.0f;
        s_ssim_computer.compute(state.images[0], state.images[1],
            [](SSIMResult result) {
                s_ssim_result = std::move(result);
                s_ssim_ready  = true;
            });
    }
    prev_diff_mode = state.diff.mode;

    // ── PSNR auto-computation ────────────────────────────────────────────────
    {
        bool need_psnr = (state.diff.mode != DiffState::Mode::None) &&
                         state.images[0].loaded && state.images[1].loaded;
        if (need_psnr && !state.diff.psnr_computed) {
            DiffExtraStats extra;
            compute_diff_stats(state.images[0], state.images[1], extra);
            // Average PSNR across RGB channels
            double sum = 0.0;
            int cnt = 0;
            for (int c = 0; c < 3; ++c) {
                if (extra.psnr[c] > 0 && extra.psnr[c] < 999.0) {
                    sum += extra.psnr[c];
                    ++cnt;
                }
            }
            state.diff.psnr_db = (cnt > 0) ? static_cast<float>(sum / cnt) : 999.0f;
            state.diff.psnr_computed = true;
        }
        if (!need_psnr) {
            state.diff.psnr_computed = false;
            state.diff.psnr_db = -1.0f;
        }
    }

    // ── Full-screen host window ───────────────────────────────────────────────
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus      |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (state.show_ui) {
        host_flags |= ImGuiWindowFlags_MenuBar;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    ImGui::Begin("##Host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    if (state.show_ui) {
        render_menubar(state);
    }

    // ── Layout: determine panel sizes ─────────────────────────────────────────
    bool two_images   = state.images[0].loaded && state.images[1].loaded;
    bool diff_mode    = (state.diff.mode != DiffState::Mode::None);
    bool overlay_mode = state.overlay.active && two_images;

    // Reserve statusbar height when UI is visible
    static constexpr float STATUSBAR_H = 24.0f;
    ImVec2 content = ImGui::GetContentRegionAvail();
    float panel_h = state.show_ui ? (content.y - STATUSBAR_H) : content.y;
    if (panel_h < 1.0f) panel_h = 1.0f;

    // Capture panels area origin for pixel balloon (used after ImGui::End())
    ImVec2 panels_origin = ImGui::GetCursorScreenPos();

    ImGuiWindowFlags child_flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // Panel screen-space rects for always-visible borders (drawn after all children)
    struct PanelBorderRect { ImVec2 min, max; ImU32 col; };
    PanelBorderRect panel_rects[3];
    int panel_rect_count = 0;

    if (overlay_mode) {
        // ── 1-panel: Overlay/Blend ───────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelOverlay", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + content.x, p.y + panel_h), IM_COL32(150, 255, 150, 200)}; }
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else if (two_images && diff_mode) {
        // ── 3-panel: A | B | Diff ────────────────────────────────────────────
        float third_w = std::floor(content.x / 3.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(third_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + third_w, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(third_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + third_w, p.y + panel_h), (ImU32)state.border_colors[1]}; }
        s_panel_right.render(state, 1, diff_renderer_, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelDiff", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); float dw = content.x - 2.0f * third_w; panel_rects[panel_rect_count++] = {p, ImVec2(p.x + dw, p.y + panel_h), (ImU32)state.border_colors[2]}; }
        s_panel_diff.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else if (two_images) {
        // ── Side-by-side: A | B ──────────────────────────────────────────────
        float half_w = std::floor(content.x * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(half_w, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + half_w, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelRight", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); float rw = content.x - half_w; panel_rects[panel_rect_count++] = {p, ImVec2(p.x + rw, p.y + panel_h), (ImU32)state.border_colors[1]}; }
        s_panel_right.render(state, 1, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        // ── Single image: full width ─────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##PanelLeft", ImVec2(0.0f, panel_h), false, child_flags);
        { ImVec2 p = ImGui::GetWindowPos(); panel_rects[panel_rect_count++] = {p, ImVec2(p.x + content.x, p.y + panel_h), (ImU32)state.border_colors[0]}; }
        s_panel_left.render(state, 0, diff_renderer_);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ── Always-visible panel borders (foreground → rendered above all children) ──
    if (state.show_borders) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImU32 shadow = IM_COL32(0, 0, 0, 160);
        for (int i = 0; i < panel_rect_count; ++i) {
            const auto& pr = panel_rects[i];
            dl->AddRect(ImVec2(pr.min.x + 1, pr.min.y + 1),
                        ImVec2(pr.max.x + 1, pr.max.y + 1), shadow, 0.0f, 0, 3.0f);
            dl->AddRect(pr.min, pr.max, pr.col, 0.0f, 0, 2.0f);
        }
    }

    // ── Statusbar ─────────────────────────────────────────────────────────────
    if (state.show_ui) {
        s_statusbar.render(state);
    }

    // ── Image info popup ──────────────────────────────────────────────────────
    if (state.show_info && (state.images[0].loaded || state.images[1].loaded)) {
        ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Image Info", &state.show_info)) {
            if (state.font_large) ImGui::PushFont(state.font_large);
            for (int i = 0; i < 2; ++i) {
                const auto& img = state.images[i];
                if (img.loaded) {
                    ImVec4 label_col = (i == 0) ? ImVec4(1,0.4f,1,1) : ImVec4(1,1,0.3f,1);
                    ImGui::TextColored(label_col, "%s:", i == 0 ? "A(Left)" : "B(Right)");
                    ImGui::SameLine();
                    ImGui::Text("%s", img.path.empty() ? "(loaded)" : img.path.c_str());
                    ImGui::Text("   %d x %d  ch=%d%s",
                                img.width, img.height, img.channels,
                                img.is_hdr ? "  [HDR]" : "");
                }
            }
            ImGui::Separator();
            for (int i = 0; i < 2; ++i) {
                const auto& v   = state.views[i];
                const auto& img = state.images[i];
                if (img.loaded) {
                    ImVec4 label_col = (i == 0) ? ImVec4(1,0.4f,1,1) : ImVec4(1,1,0.3f,1);
                    ImVec4 zoom_col  = ImVec4(0.3f, 1.0f, 1.0f, 1.0f);
                    ImGui::TextColored(label_col, "%s", i == 0 ? "A" : "B");
                    ImGui::SameLine();
                    if (v.fit) {
                        ImGui::TextColored(zoom_col, "Zoom: Fit-to-Window");
                    } else {
                        ImGui::TextColored(zoom_col, "Zoom: %.1f%%", v.zoom * 100.0f);
                        ImGui::SameLine();
                        ImGui::Text("Pan: (%.0f, %.0f)", v.pan_x, v.pan_y);
                    }
                }
            }
            if (state.font_large) ImGui::PopFont();
        }
        ImGui::End();
    }

    // ── Chart windows (floating) ───────────────────────────────────────────────
    render_histogram_window(state);
    render_hline_cut_window(state);
    render_vline_cut_window(state);
    render_stats_window(state);
    render_roi_stats_window(state);
    render_scatter_plot_window(state);
    render_hotkey_help_window(state);

    // ── Open Images window ────────────────────────────────────────────────────
    render_open_images_window(state);

    // ── Save window (context-aware) ───────────────────────────────────────────
    if (state.show_save_dialog) {
        if (state.show_histogram)
            render_chart_save_window(state, true);
        else if (state.show_hline_cut || state.show_vline_cut)
            render_chart_save_window(state, false);
        else if (state.show_stats)
            render_stats_save_window(state);
        else
            render_image_save_window(state);
    }

    ImGui::End(); // ##Host

    // ── Pixel value balloon ───────────────────────────────────────────────────
    if (state.show_pixel_info) {
        if (state.font_large) ImGui::PushFont(state.font_large);
        do {
            ImVec2 mouse = ImGui::GetMousePos();
            const ImGuiViewport* mvp = ImGui::GetMainViewport();
            float total_w = mvp->WorkSize.x;

            float px = mouse.x - panels_origin.x;
            float py = mouse.y - panels_origin.y;

            if (py < 0.0f || py >= panel_h || px < 0.0f || px >= total_w) break;

            // Determine which panel the mouse is over
            int   panel_idx    = 0;
            float panel_x_start = 0.0f;
            float panel_w_local = total_w;
            bool  is_diff_panel = false;

            char line_pos[32];
            char line_r[24], line_g[24], line_b[24];

            if (two_images && diff_mode) {
                float third_w = std::floor(total_w / 3.0f);
                if (px < third_w) {
                    panel_idx = 0; panel_x_start = 0.0f;          panel_w_local = third_w;
                } else if (px < 2.0f * third_w) {
                    panel_idx = 1; panel_x_start = third_w;        panel_w_local = third_w;
                } else {
                    is_diff_panel = true;
                    panel_x_start = 2.0f * third_w;
                    panel_w_local = total_w - 2.0f * third_w;
                }
            } else if (two_images) {
                float half_w = std::floor(total_w * 0.5f);
                if (px < half_w) {
                    panel_idx = 0; panel_x_start = 0.0f;    panel_w_local = half_w;
                } else {
                    panel_idx = 1; panel_x_start = half_w;  panel_w_local = total_w - half_w;
                }
            }

            if (is_diff_panel) {
                // diff panel: compute abs(A - B), using views[0] for coordinate mapping
                const ViewportState& bvp = state.views[0];
                const ImageEntry& imgA = state.images[state.swap_images ? 1 : 0];
                const ImageEntry& imgB = state.images[state.swap_images ? 0 : 1];
                if (!imgA.loaded || !imgB.loaded) break;

                float view_w  = panel_w_local;
                float view_h  = static_cast<float>(panel_h);
                float half_vw = view_w * 0.5f;
                float half_vh = view_h * 0.5f;
                float half_iw = imgA.width  * 0.5f;
                float half_ih = imgA.height * 0.5f;

                float local_x = px - panel_x_start;
                float local_y = py;
                float img_fx = (local_x - half_vw) / bvp.zoom - bvp.pan_x + half_iw;
                float img_fy = (local_y - half_vh) / bvp.zoom - bvp.pan_y + half_ih;

                int ix = static_cast<int>(std::floor(img_fx));
                int iy = static_cast<int>(std::floor(img_fy));
                if (ix < 0 || ix >= imgA.width || iy < 0 || iy >= imgA.height) break;
                if (ix >= imgB.width || iy >= imgB.height) break;

                int pidx_a = (iy * imgA.width + ix) * 4;
                int pidx_b = (iy * imgB.width + ix) * 4;

                std::snprintf(line_pos, sizeof(line_pos), "(%d, %d)", ix, iy);

                bool has_orig_a = imgA.ppm_maxval > 0 && !imgA.pixels_orig.empty();
                bool has_orig_b = imgB.ppm_maxval > 0 && !imgB.pixels_orig.empty();
                bool is_hdr  = imgA.is_hdr && imgB.is_hdr;
                bool has_f32 = !imgA.pixels_f32.empty() && !imgB.pixels_f32.empty();
                bool has_u8  = !imgA.pixels.empty() && !imgB.pixels.empty();

                if (has_orig_a && has_orig_b) {
                    int oidx_a = (iy * imgA.width + ix) * 3;
                    int oidx_b = (iy * imgB.width + ix) * 3;
                    fmt_balloon_pixel(line_r, sizeof(line_r), "R:",
                        std::abs((int)imgA.pixels_orig[oidx_a + 0] - (int)imgB.pixels_orig[oidx_b + 0]), state.pixel_format);
                    fmt_balloon_pixel(line_g, sizeof(line_g), "G:",
                        std::abs((int)imgA.pixels_orig[oidx_a + 1] - (int)imgB.pixels_orig[oidx_b + 1]), state.pixel_format);
                    fmt_balloon_pixel(line_b, sizeof(line_b), "B:",
                        std::abs((int)imgA.pixels_orig[oidx_a + 2] - (int)imgB.pixels_orig[oidx_b + 2]), state.pixel_format);
                } else if (is_hdr && has_f32) {
                    std::snprintf(line_r, sizeof(line_r), "R:%.3f",
                        std::fabs(imgA.pixels_f32[pidx_a + 0] - imgB.pixels_f32[pidx_b + 0]));
                    std::snprintf(line_g, sizeof(line_g), "G:%.3f",
                        std::fabs(imgA.pixels_f32[pidx_a + 1] - imgB.pixels_f32[pidx_b + 1]));
                    std::snprintf(line_b, sizeof(line_b), "B:%.3f",
                        std::fabs(imgA.pixels_f32[pidx_a + 2] - imgB.pixels_f32[pidx_b + 2]));
                } else if (has_u8) {
                    fmt_balloon_pixel(line_r, sizeof(line_r), "R:",
                        std::abs((int)imgA.pixels[pidx_a + 0] - (int)imgB.pixels[pidx_b + 0]), state.pixel_format);
                    fmt_balloon_pixel(line_g, sizeof(line_g), "G:",
                        std::abs((int)imgA.pixels[pidx_a + 1] - (int)imgB.pixels[pidx_b + 1]), state.pixel_format);
                    fmt_balloon_pixel(line_b, sizeof(line_b), "B:",
                        std::abs((int)imgA.pixels[pidx_a + 2] - (int)imgB.pixels[pidx_b + 2]), state.pixel_format);
                } else {
                    break;
                }
            } else {
                int img_idx = state.swap_images ? (1 - panel_idx) : panel_idx;
                if (img_idx < 0 || img_idx > 1) break;
                const ImageEntry&    bimg = state.images[img_idx];
                if (!bimg.loaded) break;

                const ViewportState& bvp = state.views[panel_idx];

                float view_w  = panel_w_local;
                float view_h  = static_cast<float>(panel_h);
                float half_vw = view_w * 0.5f;
                float half_vh = view_h * 0.5f;
                float half_iw = bimg.width  * 0.5f;
                float half_ih = bimg.height * 0.5f;

                // Screen → image pixel coordinates
                float local_x = px - panel_x_start;
                float local_y = py;
                float img_fx = (local_x - half_vw) / bvp.zoom - bvp.pan_x + half_iw;
                float img_fy = (local_y - half_vh) / bvp.zoom - bvp.pan_y + half_ih;

                int ix = static_cast<int>(std::floor(img_fx));
                int iy = static_cast<int>(std::floor(img_fy));
                if (ix < 0 || ix >= bimg.width || iy < 0 || iy >= bimg.height) break;

                int pidx = (iy * bimg.width + ix) * 4;

                std::snprintf(line_pos, sizeof(line_pos), "(%d, %d)", ix, iy);

                if (bimg.ppm_maxval > 0 && !bimg.pixels_orig.empty()) {
                    int oidx = (iy * bimg.width + ix) * 3;
                    fmt_balloon_pixel(line_r, sizeof(line_r), "R:", (int)bimg.pixels_orig[oidx + 0], state.pixel_format);
                    fmt_balloon_pixel(line_g, sizeof(line_g), "G:", (int)bimg.pixels_orig[oidx + 1], state.pixel_format);
                    fmt_balloon_pixel(line_b, sizeof(line_b), "B:", (int)bimg.pixels_orig[oidx + 2], state.pixel_format);
                } else if (bimg.is_hdr && !bimg.pixels_f32.empty()) {
                    std::snprintf(line_r, sizeof(line_r), "R:%.3f", bimg.pixels_f32[pidx + 0]);
                    std::snprintf(line_g, sizeof(line_g), "G:%.3f", bimg.pixels_f32[pidx + 1]);
                    std::snprintf(line_b, sizeof(line_b), "B:%.3f", bimg.pixels_f32[pidx + 2]);
                } else if (!bimg.pixels.empty()) {
                    fmt_balloon_pixel(line_r, sizeof(line_r), "R:", (int)bimg.pixels[pidx + 0], state.pixel_format);
                    fmt_balloon_pixel(line_g, sizeof(line_g), "G:", (int)bimg.pixels[pidx + 1], state.pixel_format);
                    fmt_balloon_pixel(line_b, sizeof(line_b), "B:", (int)bimg.pixels[pidx + 2], state.pixel_format);
                } else {
                    break;
                }
            }

            // Measure text for balloon sizing
            ImVec2 sz_pos = ImGui::CalcTextSize(line_pos);
            ImVec2 sz_r   = ImGui::CalcTextSize(line_r);
            ImVec2 sz_g   = ImGui::CalcTextSize(line_g);
            ImVec2 sz_b   = ImGui::CalcTextSize(line_b);

            float pad     = 12.0f;
            float gap_y   = 6.0f;
            float rgb_gap = 8.0f;
            float font_h  = ImGui::GetFontSize();

            float line2_w = sz_r.x + rgb_gap + sz_g.x + rgb_gap + sz_b.x;
            float content_w = (sz_pos.x > line2_w ? sz_pos.x : line2_w);
            float box_w   = content_w + pad * 2.0f;
            float box_h   = font_h + gap_y + font_h + pad * 2.0f;

            // Position: upper-right of cursor; clamp to screen edges
            float bx = mouse.x + 16.0f;
            float by = mouse.y - 40.0f;
            if (bx + box_w > mvp->WorkPos.x + mvp->WorkSize.x)
                bx = mouse.x - box_w - 8.0f;
            if (by < mvp->WorkPos.y)
                by = mouse.y + 16.0f;
            if (by + box_h > mvp->WorkPos.y + mvp->WorkSize.y)
                by = mvp->WorkPos.y + mvp->WorkSize.y - box_h - 4.0f;

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                              IM_COL32(20, 20, 20, 200), 6.0f);
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + box_w, by + box_h),
                        IM_COL32(80, 80, 80, 180), 6.0f);

            ImFont* cur_font = ImGui::GetFont();

            // Line 1: coordinates
            dl->AddText(cur_font, font_h, ImVec2(bx + pad, by + pad),
                        IM_COL32(220, 220, 220, 255), line_pos);

            // Line 2: R G B in channel colours
            float y2 = by + pad + font_h + gap_y;
            float x2 = bx + pad;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(255, 100, 100, 255), line_r);
            x2 += sz_r.x + rgb_gap;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(100, 255, 100, 255), line_g);
            x2 += sz_g.x + rgb_gap;
            dl->AddText(cur_font, font_h, ImVec2(x2, y2), IM_COL32(100, 130, 255, 255), line_b);
        } while (false);
        if (state.font_large) ImGui::PopFont();
    }

    // ── Zoom HUD ──────────────────────────────────────────────────────────────
    {
        float dt = ImGui::GetIO().DeltaTime;

        // Detect zoom changes (ignore fit-mode changes from window resize)
        bool zoom_changed = false;
        if (!state.views[0].fit && state.views[0].zoom != state.last_zoom_a) zoom_changed = true;
        if (!state.views[1].fit && state.views[1].zoom != state.last_zoom_b) zoom_changed = true;
        state.last_zoom_a = state.views[0].zoom;
        state.last_zoom_b = state.views[1].zoom;

        if (zoom_changed) state.zoom_hud_timer = 5.0f;

        if (state.zoom_hud_timer > 0.0f) {
            state.zoom_hud_timer -= dt;
            if (state.zoom_hud_timer < 0.0f) state.zoom_hud_timer = 0.0f;

            float alpha = (state.zoom_hud_timer > 1.0f) ? 1.0f : state.zoom_hud_timer;

            float zoom_val = state.views[state.active_panel].zoom;
            char buf[32];
            if (zoom_val >= 1.0f)
                std::snprintf(buf, sizeof(buf), "%.0fx", zoom_val);
            else
                std::snprintf(buf, sizeof(buf), "%.2fx", zoom_val);

            ImVec2 text_size = ImGui::CalcTextSize(buf);
            float pad_x = 16.0f, pad_y = 8.0f;
            float pill_w = text_size.x + pad_x * 2.0f;
            float pill_h = text_size.y + pad_y * 2.0f;

            const ImGuiViewport* mvp = ImGui::GetMainViewport();
            ImVec2 pill_pos(mvp->WorkPos.x + (mvp->WorkSize.x - pill_w) * 0.5f,
                            mvp->WorkPos.y + 12.0f);

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(pill_pos,
                              ImVec2(pill_pos.x + pill_w, pill_pos.y + pill_h),
                              IM_COL32(0, 0, 0, (int)(180 * alpha)),
                              pill_h * 0.5f);
            dl->AddText(ImVec2(pill_pos.x + pad_x, pill_pos.y + pad_y),
                        IM_COL32(255, 255, 255, (int)(255 * alpha)),
                        buf);
        }
    }
}
