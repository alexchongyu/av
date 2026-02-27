#include "statusbar.h"

#include <imgui.h>
#include <cstdio>

static const char* diff_mode_name(DiffState::Mode m) {
    switch (m) {
    case DiffState::Mode::None:          return "None";
    case DiffState::Mode::PixelAbsolute: return "Abs";
    case DiffState::Mode::PixelRelative: return "Rel";
    case DiffState::Mode::FalseColor:    return "FalseColor";
    case DiffState::Mode::SSIM:          return "SSIM";
    default:                             return "?";
    }
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
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "Diff: %s  x%.1f",
                           diff_mode_name(state.diff.mode),
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
