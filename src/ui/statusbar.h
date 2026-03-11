#pragma once

#include "../app.h"

// ─── Status bar ───────────────────────────────────────────────────────────────
// Renders a thin bar at the bottom of the window showing:
//   - zoom level
//   - image info (resolution, channels)
//   - diff mode & amplify
//   - SSIM score (if computed)
//   - pixel value under cursor (if show_pixel_info)

class StatusBar {
public:
    StatusBar() = default;

    // Render status bar. Call within an ImGui window context.
    void render(const AppState& state);
};
