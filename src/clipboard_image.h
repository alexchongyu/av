#pragma once

struct AppState;

// Copy image to system clipboard as PNG.
// target_type: 0=Image A, 1=Image B, 2=Diff (current diff mode)
// Respects state.swap_images. No-op if target image is not loaded.
// Returns true on success.
bool clipboard_copy_image(AppState& state, int target_type);
