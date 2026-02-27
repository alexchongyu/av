#pragma once

#include "app.h"

// ─── Zoom levels ──────────────────────────────────────────────────────────────
// All power-of-2: 1/8 → 256×
inline constexpr float ZOOM_LEVELS[] = {
    0.125f, 0.25f, 0.5f,
    1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f
};
inline constexpr int NUM_ZOOM_LEVELS = 12;

// ─── Viewport operations ──────────────────────────────────────────────────────

// Set zoom so the whole image fits in the viewport; clears pan.
void viewport_fit(ViewportState& v, int img_w, int img_h, int view_w, int view_h);

// Zoom in / out by one step in ZOOM_LEVELS.
void viewport_zoom_in (ViewportState& v);
void viewport_zoom_out(ViewportState& v);

// Set exact zoom (clamped to ZOOM_LEVELS range).
void viewport_set_zoom(ViewportState& v, float zoom);

// Pan by (dx, dy) image-pixels.
void viewport_pan(ViewportState& v, float dx, float dy);

// Reset pan to centre.
void viewport_center(ViewportState& v);

// Clamp pan so the image stays at least partially visible.
void viewport_clamp_pan(ViewportState& v,
                        int img_w, int img_h,
                        int view_w, int view_h);
