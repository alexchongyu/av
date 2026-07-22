#pragma once

#include "app.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

// ─── FLIP (ꟻLIP-LDR) perceptual difference ────────────────────────────────────
// Faithful reimplementation of NVIDIA's LDR-FLIP (Andersson et al. 2020):
// sRGB -> YCxCz, contrast-sensitivity spatial filtering, HyAB colour distance in
// Hunt-adjusted CIELab, edge/point feature detection, combined into a per-pixel
// perceptual error map in [0,1] (0 = identical). Visualised with the magma
// colormap. Runs purely on the CPU (no GL).
//
// Default ppd (pixels-per-degree) = 0.7 m viewing of a 0.7 m-wide 4K monitor.
constexpr float FLIP_DEFAULT_PPD = 67.0206f;

struct FLIPResult {
    float                score = -1.0f;  // mean FLIP error in [0,1]; 0 = identical
    std::vector<uint8_t> rgba;           // magma-coloured error map, RGBA8 (w*h*4)
    int                  w = 0, h = 0;
    bool                 success = false;
};

// Synchronous LDR-FLIP on the calling thread (headless / --metrics).
FLIPResult compute_flip(const ImageEntry& reference, const ImageEntry& test,
                        float ppd = FLIP_DEFAULT_PPD);

// ─── Async FLIP computer ──────────────────────────────────────────────────────
// Mirrors SSIMComputer: runs compute_flip on a background std::jthread and calls
// the callback with the result. GPU upload of the heatmap must happen on the
// main thread by the caller.
class FLIPComputer {
public:
    FLIPComputer()  = default;
    ~FLIPComputer() { cancel(); }

    void compute(const ImageEntry& a, const ImageEntry& b,
                 std::function<void(FLIPResult)> callback,
                 float ppd = FLIP_DEFAULT_PPD);

    void cancel();
    bool is_running() const { return running_.load(); }

private:
    std::jthread      thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_requested_{false};
};
