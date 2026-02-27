#pragma once

#include "app.h"
#include "gl_texture.h"

#include <functional>
#include <thread>
#include <atomic>
#include <vector>

// ─── GPU diff renderer ────────────────────────────────────────────────────────
// Renders a diff visualisation into the currently-bound FBO.

class DiffRenderer {
public:
    DiffRenderer()  = default;
    ~DiffRenderer() = default;

    // Must be called once after GL context is created.
    bool init();

    // Render diff into the currently-bound FBO.
    // view_w/h: FBO dimensions. img_w/h: source image dimensions.
    void render(GLuint texA, GLuint texB,
                const ViewportState& view,
                int img_w,  int img_h,
                int view_w, int view_h,
                DiffState::Mode mode, float amplify);

    bool valid() const { return shader_.valid(); }

private:
    ShaderProgram shader_;
    ScreenQuad    quad_;
};

// ─── SSIM result ──────────────────────────────────────────────────────────────
struct SSIMResult {
    float              score   = 0.0f;  // 0 (different) – 1 (identical)
    std::vector<float> heatmap;         // per-pixel dissimilarity in [0,1]
    int                w       = 0;
    int                h       = 0;
    bool               success = false;
};

// ─── Async SSIM computer ──────────────────────────────────────────────────────
// Runs SSIM in a background std::jthread; calls callback on completion.
// GPU upload of the heatmap texture must be done on the main thread.

class SSIMComputer {
public:
    SSIMComputer()  = default;
    ~SSIMComputer() { cancel(); }

    // Start (or restart) computation.
    // Callback is called from the background thread with the result.
    void compute(const ImageEntry& a,
                 const ImageEntry& b,
                 std::function<void(SSIMResult)> callback);

    // Request cancellation; blocks until thread exits.
    void cancel();

    bool is_running() const { return running_.load(); }

private:
    std::jthread        thread_;
    std::atomic<bool>   running_{false};
    std::atomic<bool>   cancel_requested_{false};
};
