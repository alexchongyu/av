#include "viewport.h"

#include <algorithm>
#include <cmath>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static int nearest_zoom_index(float zoom) {
    // Find the closest index in ZOOM_LEVELS
    int best = 0;
    float best_diff = std::abs(ZOOM_LEVELS[0] - zoom);
    for (int i = 1; i < NUM_ZOOM_LEVELS; ++i) {
        float d = std::abs(ZOOM_LEVELS[i] - zoom);
        if (d < best_diff) { best_diff = d; best = i; }
    }
    return best;
}

// ─── Implementation ───────────────────────────────────────────────────────────

void viewport_fit(ViewportState& v, int img_w, int img_h, int view_w, int view_h) {
    if (img_w <= 0 || img_h <= 0 || view_w <= 0 || view_h <= 0) return;
    float sx = static_cast<float>(view_w) / static_cast<float>(img_w);
    float sy = static_cast<float>(view_h) / static_cast<float>(img_h);
    v.zoom  = std::min(sx, sy);
    v.pan_x = 0.0f;
    v.pan_y = 0.0f;
    v.fit   = true;
}

void viewport_zoom_in(ViewportState& v) {
    int idx = nearest_zoom_index(v.zoom);
    // Go to next higher level
    if (idx < NUM_ZOOM_LEVELS - 1) {
        // Prefer strictly higher
        if (ZOOM_LEVELS[idx] <= v.zoom && idx + 1 < NUM_ZOOM_LEVELS)
            ++idx;
        else if (ZOOM_LEVELS[idx] > v.zoom)
            { /* idx already above */ }
        else
            ++idx;
    }
    v.zoom = ZOOM_LEVELS[std::min(idx, NUM_ZOOM_LEVELS - 1)];
    v.fit  = false;
}

void viewport_zoom_out(ViewportState& v) {
    int idx = nearest_zoom_index(v.zoom);
    if (idx > 0) {
        if (ZOOM_LEVELS[idx] >= v.zoom && idx - 1 >= 0)
            --idx;
    }
    v.zoom = ZOOM_LEVELS[std::max(idx, 0)];
    v.fit  = false;
}

void viewport_set_zoom(ViewportState& v, float zoom) {
    v.zoom = std::clamp(zoom, ZOOM_LEVELS[0], ZOOM_LEVELS[NUM_ZOOM_LEVELS - 1]);
    v.fit  = false;
}

void viewport_pan(ViewportState& v, float dx, float dy) {
    v.pan_x = std::round(v.pan_x + dx);
    v.pan_y = std::round(v.pan_y + dy);
    v.fit    = false;
}

void viewport_center(ViewportState& v) {
    v.pan_x = 0.0f;
    v.pan_y = 0.0f;
}

void viewport_clamp_pan(ViewportState& v,
                        int img_w, int img_h,
                        int view_w, int view_h) {
    if (img_w <= 0 || img_h <= 0) return;
    // Half the visible region in image-pixels
    float half_vw = static_cast<float>(view_w) * 0.5f / v.zoom;
    float half_vh = static_cast<float>(view_h) * 0.5f / v.zoom;
    float half_iw = static_cast<float>(img_w)  * 0.5f;
    float half_ih = static_cast<float>(img_h)  * 0.5f;

    // Allow at least 50 pixels of the image to stay visible
    constexpr float MARGIN = 50.0f;
    float max_x = half_iw + half_vw - MARGIN / v.zoom;
    float max_y = half_ih + half_vh - MARGIN / v.zoom;

    v.pan_x = std::round(std::clamp(v.pan_x, -max_x, max_x));
    v.pan_y = std::round(std::clamp(v.pan_y, -max_y, max_y));
}
