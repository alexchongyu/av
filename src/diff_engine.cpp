#include "diff_engine.h"
#include "shader_sources.h"
#include "render_backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>

// ─── DiffRenderer ─────────────────────────────────────────────────────────────

bool DiffRenderer::init() {
    if (is_software_mode()) return true;  // No GPU shaders in software mode

    if (!shader_.compile(shaders::VERTEX_SRC, shaders::DIFF_FRAG_SRC)) {
        std::cerr << "[DiffRenderer] shader compilation failed\n";
        return false;
    }
    quad_.init();

    // Bind attribute locations by name (requires re-link, done in compile)
    // The vertex shader uses layout locations implicitly via glVertexAttribPointer
    // in ScreenQuad::init().  We need to set them before linking.
    // For #version 150 (no explicit layout), we bind them now:
    glBindAttribLocation(shader_.id, 0, "a_pos");
    glBindAttribLocation(shader_.id, 1, "a_uv");
    // Re-link after binding attributes
    glLinkProgram(shader_.id);
    GLint ok = 0;
    glGetProgramiv(shader_.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::cerr << "[DiffRenderer] re-link failed\n";
        return false;
    }
    return true;
}

void DiffRenderer::render(uintptr_t texA, uintptr_t texB,
                          const ViewportState& view,
                          int img_w,  int img_h,
                          int view_w, int view_h,
                          DiffState::Mode mode, float amplify,
                          ChannelMode channel) {
    if (!shader_.valid()) return;

    int diff_mode_int = 0;
    switch (mode) {
        case DiffState::Mode::PixelAbsolute: diff_mode_int = 0; break;
        case DiffState::Mode::PixelRelative: diff_mode_int = 1; break;
        case DiffState::Mode::FalseColor:    diff_mode_int = 2; break;
        default: break;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texA));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texB));

    shader_.use();
    shader_.set_int  ("u_texA",        0);
    shader_.set_int  ("u_texB",        1);
    shader_.set_vec2 ("u_image_size",  static_cast<float>(img_w),  static_cast<float>(img_h));
    shader_.set_vec2 ("u_view_size",   static_cast<float>(view_w), static_cast<float>(view_h));
    shader_.set_float("u_zoom",        view.zoom);
    shader_.set_vec2 ("u_pan",         view.pan_x, view.pan_y);
    shader_.set_int  ("u_diff_mode",   diff_mode_int);
    shader_.set_float("u_amplify",     amplify);
    shader_.set_int  ("u_channel",    static_cast<int>(channel));

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    quad_.draw();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─── SSIM implementation ──────────────────────────────────────────────────────
// Simple 11×11 Gaussian window SSIM on luminance channel.

namespace {

constexpr int    SSIM_WIN   = 11;
constexpr double SSIM_C1    = (0.01 * 255.0) * (0.01 * 255.0);
constexpr double SSIM_C2    = (0.03 * 255.0) * (0.03 * 255.0);

// Build 11×11 Gaussian kernel (σ=1.5), normalised to sum=1.
static std::vector<double> make_gaussian() {
    const double sigma = 1.5;
    std::vector<double> k(SSIM_WIN * SSIM_WIN);
    double sum = 0.0;
    int    half = SSIM_WIN / 2;
    for (int y = 0; y < SSIM_WIN; ++y) {
        for (int x = 0; x < SSIM_WIN; ++x) {
            double dx = x - half, dy = y - half;
            double v  = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
            k[y * SSIM_WIN + x] = v;
            sum += v;
        }
    }
    for (auto& v : k) v /= sum;
    return k;
}

// Convert RGBA8 pixel at (x,y) → luminance [0,255].
inline double luma(const uint8_t* pixels, int w, int x, int y) {
    const uint8_t* p = pixels + (y * w + x) * 4;
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

// Weighted window statistics
struct WinStats { double mu = 0, sigma2 = 0, sigma = 0; };
struct WinCross { double sigma_ab = 0; };

static WinStats window_stats(const uint8_t* img, int w, int h,
                              const std::vector<double>& kernel,
                              int cx, int cy) {
    int half = SSIM_WIN / 2;
    double mu = 0.0, mu2 = 0.0;
    for (int ky = 0; ky < SSIM_WIN; ++ky) {
        for (int kx = 0; kx < SSIM_WIN; ++kx) {
            int ix = std::clamp(cx + kx - half, 0, w - 1);
            int iy = std::clamp(cy + ky - half, 0, h - 1);
            double v = luma(img, w, ix, iy);
            double w_ = kernel[ky * SSIM_WIN + kx];
            mu  += w_ * v;
            mu2 += w_ * v * v;
        }
    }
    WinStats s;
    s.mu     = mu;
    s.sigma2 = mu2 - mu * mu;
    if (s.sigma2 < 0) s.sigma2 = 0;
    s.sigma  = std::sqrt(s.sigma2);
    return s;
}

static double window_cross(const uint8_t* imgA, const uint8_t* imgB,
                            int w, int h,
                            const std::vector<double>& kernel,
                            int cx, int cy,
                            double muA, double muB) {
    int half = SSIM_WIN / 2;
    double cov = 0.0;
    for (int ky = 0; ky < SSIM_WIN; ++ky) {
        for (int kx = 0; kx < SSIM_WIN; ++kx) {
            int ix = std::clamp(cx + kx - half, 0, w - 1);
            int iy = std::clamp(cy + ky - half, 0, h - 1);
            double va = luma(imgA, w, ix, iy);
            double vb = luma(imgB, w, ix, iy);
            double ww = kernel[ky * SSIM_WIN + kx];
            cov += ww * (va - muA) * (vb - muB);
        }
    }
    return cov;
}

SSIMResult compute_ssim_cpu(const ImageEntry& a,
                             const ImageEntry& b,
                             std::atomic<bool>& cancel_flag) {
    SSIMResult res;
    if (!a.loaded || !b.loaded) return res;
    if (a.pixels.empty() || b.pixels.empty()) return res;

    int w = std::min(a.width,  b.width);
    int h = std::min(a.height, b.height);
    if (w <= 0 || h <= 0) return res;

    auto kernel = make_gaussian();
    res.w       = w;
    res.h       = h;
    res.heatmap.resize(static_cast<size_t>(w) * h, 0.0f);

    double total = 0.0;
    long   count = 0;

    for (int y = 0; y < h; ++y) {
        if (cancel_flag.load()) return res;
        for (int x = 0; x < w; ++x) {
            auto sA = window_stats(a.pixels.data(), w, h, kernel, x, y);
            auto sB = window_stats(b.pixels.data(), w, h, kernel, x, y);
            double cov = window_cross(a.pixels.data(), b.pixels.data(),
                                      w, h, kernel, x, y, sA.mu, sB.mu);
            double num   = (2.0 * sA.mu * sB.mu + SSIM_C1) *
                           (2.0 * cov           + SSIM_C2);
            double denom = (sA.mu * sA.mu + sB.mu * sB.mu + SSIM_C1) *
                           (sA.sigma2 + sB.sigma2 + SSIM_C2);
            double ssim  = (denom > 1e-12) ? num / denom : 1.0;
            ssim          = std::clamp(ssim, -1.0, 1.0);
            // Dissimilarity: 0=same, 1=different
            float diff    = static_cast<float>((1.0 - ssim) / 2.0);
            res.heatmap[static_cast<size_t>(y) * w + x] = diff;
            total += ssim;
            ++count;
        }
    }

    res.score   = static_cast<float>(count > 0 ? total / count : 0.0);
    res.success = true;
    return res;
}

} // namespace

// ─── SSIMComputer ─────────────────────────────────────────────────────────────

void SSIMComputer::cancel() {
    cancel_requested_.store(true);
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    running_.store(false);
    cancel_requested_.store(false);
}

void SSIMComputer::compute(const ImageEntry& a,
                           const ImageEntry& b,
                           std::function<void(SSIMResult)> callback) {
    cancel();

    // Copy pixel data for background thread
    ImageEntry a_copy = a;
    ImageEntry b_copy = b;
    // Clear GPU handle (not needed in background)
    a_copy.texture_id = 0;
    b_copy.texture_id = 0;

    running_.store(true);
    cancel_requested_.store(false);

    thread_ = std::jthread([this,
                            ac = std::move(a_copy),
                            bc = std::move(b_copy),
                            cb = std::move(callback)](std::stop_token st) {
        (void)st;
        SSIMResult result = compute_ssim_cpu(ac, bc, cancel_requested_);
        running_.store(false);
        if (!cancel_requested_.load()) {
            cb(std::move(result));
        }
    });
}
