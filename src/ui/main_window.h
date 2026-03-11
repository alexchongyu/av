#pragma once

#include "../app.h"
#include "../diff_engine.h"

// Forward declarations
struct SDL_Window;

// ─── Main window ──────────────────────────────────────────────────────────────
// Owns the ImGui dockspace and menu bar.
// image_panel and statusbar are rendered from within this context.

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow() = default;

    // Must be called after ImGui is initialised.
    bool init();

    // Render full ImGui frame (dockspace + menubar + child panels).
    // Call between ImGui::NewFrame() and ImGui::Render().
    void render(AppState& state);

private:
    void render_menubar(AppState& state);

    DiffRenderer diff_renderer_;
    bool         inited_ = false;
};
