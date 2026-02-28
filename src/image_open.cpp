#include "image_open.h"
#include "image_loader.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <string>

// ─── SDL dialog callback ──────────────────────────────────────────────────────

struct OpenDialogUserData {
    OpenDialogState* open_state;
    int target;   // 0 or 1
};

static void SDLCALL open_dialog_callback(void* userdata,
                                         const char* const* filelist,
                                         int /*filter*/)
{
    auto* ud = static_cast<OpenDialogUserData*>(userdata);
    if (filelist && filelist[0] && filelist[0][0] != '\0') {
        ud->open_state->opened_path  = filelist[0];
        ud->open_state->open_target  = ud->target;
        ud->open_state->open_pending = true;
    }
    delete ud;
}

void open_open_file_dialog(AppState& state, int target)
{
    SDL_DialogFileFilter filters[] = {
        {"Image files", "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.ppm;*.pgm"},
        {"All files",   "*"},
    };

    auto* ud = new OpenDialogUserData{&state.open_state, target};
    SDL_ShowOpenFileDialog(open_dialog_callback, ud,
                           state.window, filters, 2, nullptr, false);
}

// ─── Open Images window ───────────────────────────────────────────────────────

void render_open_images_window(AppState& state)
{
    if (!state.open_state.show) return;

    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Open Images", &state.open_state.show)) {
        ImGui::End();
        return;
    }

    auto draw_image_row = [&](int idx, const char* label) {
        ImGui::Text("%s", label);

        const auto& img = state.images[idx];
        if (img.loaded) {
            const char* fname = img.path.empty() ? "(loaded)"
                              : (img.path.rfind('/') != std::string::npos
                                  ? img.path.c_str() + img.path.rfind('/') + 1
                                  : img.path.c_str());
            ImGui::TextDisabled("  Current: %s  %dx%d", fname, img.width, img.height);
        } else {
            ImGui::TextDisabled("  Current: (none)");
        }

        char btn_label[32];
        std::snprintf(btn_label, sizeof(btn_label), "Open %s...", idx == 0 ? "A" : "B");
        if (ImGui::Button(btn_label)) {
            open_open_file_dialog(state, idx);
        }
    };

    draw_image_row(0, "Image A (Left)");
    ImGui::Spacing();
    draw_image_row(1, "Image B (Right)");
    ImGui::Separator();

    ImGui::Checkbox("Clear other image when loading single", &state.open_state.clear_other);
    ImGui::Separator();

    float close_w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - close_w + ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Close")) {
        state.open_state.show = false;
    }

    ImGui::End();
}
