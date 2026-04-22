#include "clipboard_image.h"
#include "app.h"
#include "image_save.h"

#include <SDL3/SDL.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <vector>

namespace {

// Payload heap-allocated per SDL_SetClipboardData call; ownership is handed
// to SDL and released in clipboard_cleanup_callback. Using per-call heap
// storage (instead of a file-global shared_ptr) lets SDL invoke cleanup on
// the *previous* payload when a new one is set — without touching the new
// one — so back-to-back copies stay valid.
struct ClipboardPngData {
    std::vector<uint8_t> bytes;
};

void stbi_mem_write_func(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    const uint8_t* b = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), b, b + size);
}

const void* SDLCALL clipboard_data_callback(void* userdata,
                                             const char* mime_type,
                                             size_t* size) {
    auto* d = static_cast<ClipboardPngData*>(userdata);
    if (d && std::strcmp(mime_type, "image/png") == 0) {
        *size = d->bytes.size();
        return d->bytes.data();
    }
    *size = 0;
    return nullptr;
}

void SDLCALL clipboard_cleanup_callback(void* userdata) {
    delete static_cast<ClipboardPngData*>(userdata);
}

} // namespace

bool clipboard_copy_image(AppState& state, int target_type) {
    const uint8_t* rgba_data = nullptr;
    std::vector<uint8_t> rgba_buf;
    int w = 0, h = 0;

    if (target_type == 0 || target_type == 1) {
        int idx = state.swap_images ? (1 - target_type) : target_type;
        const ImageEntry& img = state.images[idx];
        if (!img.loaded) return false;
        w = img.width;
        h = img.height;

        if (!img.pixels.empty()) {
            rgba_data = img.pixels.data();
        } else if (!img.pixels_f32.empty()) {
            rgba_buf.resize(static_cast<size_t>(w) * h * 4);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                for (int c = 0; c < 4; ++c) {
                    float v = std::clamp(img.pixels_f32[i * 4 + c], 0.0f, 1.0f);
                    rgba_buf[i * 4 + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
                }
            }
            rgba_data = rgba_buf.data();
        } else {
            return false;
        }
    } else if (target_type == 2) {
        int idx_a = state.swap_images ? 1 : 0;
        int idx_b = state.swap_images ? 0 : 1;
        const auto& imgA = state.images[idx_a];
        const auto& imgB = state.images[idx_b];
        if (!imgA.loaded || !imgB.loaded) return false;
        rgba_buf = compute_diff_cpu(imgA, imgB, state.diff);
        w = std::min(imgA.width, imgB.width);
        h = std::min(imgA.height, imgB.height);
        rgba_data = rgba_buf.data();
    } else {
        return false;
    }

    auto* payload = new ClipboardPngData();
    stbi_write_png_to_func(stbi_mem_write_func, &payload->bytes, w, h, 4,
                           rgba_data, w * 4);
    if (payload->bytes.empty()) {
        delete payload;
        return false;
    }

    const char* mime_types[] = {"image/png"};
    if (!SDL_SetClipboardData(clipboard_data_callback,
                              clipboard_cleanup_callback,
                              payload, mime_types, 1)) {
        // Registration failed → SDL will not invoke cleanup; release manually.
        delete payload;
        return false;
    }
    return true;
}
