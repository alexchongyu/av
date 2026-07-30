#include "statusbar.h"

#include <imgui.h>
#include <cstdio>

// Driven by kDiffModes (app.h) so the statusbar can never miss a mode (incl. AlphaBlend).
static const char* diff_mode_name(DiffState::Mode m) {
    return diff_short_tag(m);
}

void StatusBar::render(const AppState& state) {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::BeginChild("##statusbar", ImVec2(0, 24), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);

    // Zoom
    const ViewportState& vp = state.views[state.active_panel];
    ImGui::Text("Zoom: %.0f%%", vp.zoom * 100.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Image A info
    const ImageEntry& imgA = state.images[0];
    if (imgA.loaded) {
        ImGui::Text("A: %dx%d", imgA.width, imgA.height);
    } else {
        ImGui::TextDisabled("A: —");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Image B info
    const ImageEntry& imgB = state.images[1];
    if (imgB.loaded) {
        ImGui::Text("B: %dx%d", imgB.width, imgB.height);
    } else {
        ImGui::TextDisabled("B: —");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Diff mode
    if (state.diff.mode != DiffState::Mode::None) {
        const char* dir = state.swap_images ? "B-A" : "A-B";
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "Diff: %s (%s)  x%.1f",
                           diff_mode_name(state.diff.mode),
                           dir,
                           state.diff.amplify);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // SSIM score
    if (state.diff.mode == DiffState::Mode::SSIM) {
        if (state.diff.ssim_computing) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "SSIM: computing…");
        } else if (state.diff.ssim_score >= 0.0f) {
            float s = state.diff.ssim_score;
            ImVec4 col = (s > 0.99f) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                       : (s > 0.90f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f)
                                     : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(col, "SSIM: %.4f", s);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // FLIP score (lower = better; 0 = identical)
    if (state.diff.mode == DiffState::Mode::FLIP) {
        if (state.diff.flip_computing) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "FLIP: computing…");
        } else if (state.diff.flip_score >= 0.0f) {
            float f = state.diff.flip_score;
            ImVec4 col = (f < 0.05f) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                       : (f < 0.15f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f)
                                     : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(col, "FLIP: %.4f", f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // --comp: PSNR 2종 + 블록 승패 요약 (항상 표시)
    if (state.comp.active && state.comp.computed) {
        const CompState& cs = state.comp;
        auto pcol = [](float p) {
            return (p > 40.0f) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                 : (p > 30.0f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f)
                               : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        };
        if (!cs.mismatch2 && cs.psnr2 >= 0.0f) {
            if (cs.psnr2 >= 999.0f)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "P2 inf");
            else
                ImGui::TextColored(pcol(cs.psnr2), "P2 %.1fdB", cs.psnr2);
            ImGui::SameLine();
        }
        if (!cs.mismatch3 && cs.psnr3 >= 0.0f) {
            if (cs.psnr3 >= 999.0f)
                ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "P3 inf");
            else
                ImGui::TextColored(pcol(cs.psnr3), "P3 %.1fdB", cs.psnr3);
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "W2 %d", cs.win2);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "W3 %d", cs.win3);
        ImGui::SameLine();
        ImGui::TextDisabled("T %d", cs.tie);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // PSNR (auto-computed with diff mode)
    if (state.diff.mode != DiffState::Mode::None && state.diff.psnr_computed && state.diff.psnr_db > 0) {
        float p = state.diff.psnr_db;
        ImVec4 col = (p > 40.0f) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                   : (p > 30.0f) ? ImVec4(1.0f, 0.9f, 0.2f, 1.0f)
                                 : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        if (p >= 999.0f)
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "PSNR: inf");
        else
            ImGui::TextColored(col, "PSNR: %.1f dB", p);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Highlight diff pixel count
    if (state.diff.mode == DiffState::Mode::Highlight &&
        state.diff.threshold_total_count > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
            "[Diff: %d / %d px]",
            state.diff.threshold_exceed_count,
            state.diff.threshold_total_count);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Tolerance threshold
    if (state.diff.mode != DiffState::Mode::None && state.diff.threshold > 0) {
        float pct = (state.diff.threshold_total_count > 0)
            ? state.diff.threshold_exceed_count * 100.0f / state.diff.threshold_total_count
            : 0.0f;
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            "Thr: %d (%.1f%% exceed)", state.diff.threshold, pct);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Sequence info
    for (int i = 0; i < 2; ++i) {
        const auto& seq = state.sequences[i];
        if (!seq.files.empty() && seq.current_index >= 0) {
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                "%s [%d/%d]", (i == 0) ? "A" : "B",
                seq.current_index + 1, (int)seq.files.size());
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }
    }

    // ROI 모드 표시
    if (state.roi.active) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ROI mode");
        if (state.roi.has_roi)
            ImGui::SameLine(), ImGui::TextDisabled("[%d,%d %dx%d]",
                state.roi.x, state.roi.y, state.roi.w, state.roi.h);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Overlay 모드 표시
    if (state.overlay.active) {
        const char* mode_str = (state.overlay.mode == OverlayState::Mode::Curtain)
            ? "Curtain" : "Blend";
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
            "Overlay:%s  %.0f%%", mode_str, state.overlay.alpha * 100.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Slideshow indicator
    if (state.slideshow.active) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f),
            "\xe2\x96\xb6 %.1fs / %.1fs", state.slideshow.countdown, state.slideshow.interval);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Blink comparator indicator
    if (state.blink.active) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            "\xe2\x97\x89 BLINK %s  %.2fs", state.blink.show_b ? "B" : "A", state.blink.interval);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Crosshair indicator
    if (state.show_crosshair) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Crosshair");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    // Sync indicator
    ImGui::TextDisabled("Sync: %s", state.sync_viewports ? "on" : "off");
    ImGui::SameLine();

    // FPS (right-aligned)
    char fps_buf[32];
    std::snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", io.Framerate);
    float fps_w = ImGui::CalcTextSize(fps_buf).x;
    float avail  = ImGui::GetContentRegionAvail().x;
    if (avail > fps_w + 8.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - fps_w - 4.0f);
    }
    ImGui::TextDisabled("%s", fps_buf);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
