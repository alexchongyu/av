#include "metrics_cli.h"

#include "app.h"           // ImageEntry, CliOptions
#include "image_loader.h"  // scan_image_directory
#include "chart_export.h"  // compute_diff_stats, DiffExtraStats
#include "diff_engine.h"   // compute_ssim, SSIMResult
#include "flip_engine.h"   // compute_flip, FLIPResult

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CPU-only decode into an ImageEntry (no texture upload, no GL/SDL/ImGui).
// Mirrors the decode half of load_image() (image_loader.cpp): stbi_loadf for
// HDR → pixels_f32, else stbi_load → pixels (RGBA8). channels forced to 4.
bool decode_image_cpu(const std::string& path, ImageEntry& e) {
    e = {};
    e.path   = path;
    e.is_hdr = (stbi_is_hdr(path.c_str()) != 0);

    int w = 0, h = 0, ch = 0;
    if (e.is_hdr) {
        float* d = stbi_loadf(path.c_str(), &w, &h, &ch, 4);
        if (!d) {
            std::cerr << "[metrics] failed to load HDR: " << path
                      << " — " << stbi_failure_reason() << "\n";
            return false;
        }
        e.pixels_f32.assign(d, d + static_cast<size_t>(w) * h * 4);
        stbi_image_free(d);
    } else {
        uint8_t* d = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!d) {
            std::cerr << "[metrics] failed to load: " << path
                      << " — " << stbi_failure_reason() << "\n";
            return false;
        }
        e.pixels.assign(d, d + static_cast<size_t>(w) * h * 4);
        stbi_image_free(d);
    }
    e.width    = w;
    e.height   = h;
    e.channels = 4;
    e.loaded   = true;
    return true;
}

// CSV field formatter: +inf → "inf" (identical images), else %.6g.
std::string fmtv(double v) {
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

struct Metrics {
    bool   ok       = false;
    bool   mismatch = false;
    int    w = 0, h = 0;
    double psnr = 0, ssim = 0, flip = 0, mse = 0, mae = 0, maxerr = 0;
    double psnr_r = 0, psnr_g = 0, psnr_b = 0, psnr_y = 0;  // per-channel + Rec.709 luma PSNR
    double msigned = 0;                                     // luma-weighted mean signed error (A-B), bias direction
};

// Compute overall metrics for one A/B pair (both already CPU-decoded).
// Overall MSE/MAE = mean over RGB channels; MaxErr = max over channels.
// PSNR mirrors compute_info_psnr() (app.cpp): average of channels whose PSNR is
// finite and in (0,999); if none (all channels identical) → +inf.
Metrics compute_pair_metrics(const ImageEntry& A, const ImageEntry& B) {
    Metrics m;
    m.w = A.width;
    m.h = A.height;

    bool bothU8  = !A.pixels.empty()     && !B.pixels.empty();
    bool bothF32 = !A.pixels_f32.empty() && !B.pixels_f32.empty();
    if (A.width != B.width || A.height != B.height || (!bothU8 && !bothF32)) {
        m.mismatch = true;
        return m;
    }

    DiffExtraStats ex;
    compute_diff_stats(A, B, ex);

    double mse = 0, mae = 0, maxe = 0;
    for (int c = 0; c < 3; ++c) {
        mse += ex.mse[c];
        mae += ex.mae[c];
        maxe = std::max(maxe, ex.max_error[c]);
    }
    m.mse    = mse / 3.0;
    m.mae    = mae / 3.0;
    m.maxerr = maxe;

    double psum = 0.0;
    int    pcnt = 0;
    for (int c = 0; c < 3; ++c) {
        if (ex.psnr[c] > 0.0 && ex.psnr[c] < 999.0) { psum += ex.psnr[c]; ++pcnt; }
    }
    m.psnr = (pcnt > 0) ? (psum / pcnt) : std::numeric_limits<double>::infinity();

    m.psnr_r = ex.psnr[0]; m.psnr_g = ex.psnr[1]; m.psnr_b = ex.psnr[2];
    m.psnr_y = ex.psnr_y;
    m.msigned = 0.2126*ex.mean_signed[0] + 0.7152*ex.mean_signed[1] + 0.0722*ex.mean_signed[2];

    SSIMResult s = compute_ssim(A, B);
    m.ssim = s.success ? s.score : 0.0;

    FLIPResult f = compute_flip(A, B);
    m.flip = f.success ? f.score : 0.0;

    m.ok   = true;
    return m;
}

// CSV columns (14): file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned
constexpr const char* CSV_HEADER =
    "file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned";
// Placeholder row for missing/mismatch/decode_error: <name>,,,<tag>, then 10 empty fields.
static std::string placeholder_row(const std::string& name, const char* tag) {
    return name + ",,," + tag + ",,,,,,,,,,";
}

void print_header() {
    std::cout << CSV_HEADER << '\n';
}

void print_row(const std::string& name, const Metrics& m) {
    if (m.mismatch) {
        std::cout << placeholder_row(name, "mismatch") << '\n';
        return;
    }
    std::cout << name << ',' << m.w << ',' << m.h << ','
              << fmtv(m.psnr)   << ',' << fmtv(m.psnr_r) << ',' << fmtv(m.psnr_g) << ','
              << fmtv(m.psnr_b) << ',' << fmtv(m.psnr_y) << ',' << fmtv(m.ssim)   << ','
              << fmtv(m.flip)   << ',' << fmtv(m.mse)    << ',' << fmtv(m.mae)    << ','
              << fmtv(m.maxerr) << ',' << fmtv(m.msigned) << '\n';
}

} // namespace

int run_metrics_headless(const CliOptions& cli, const std::string& pair_dir_b) {
    print_header();

    if (cli.pair) {
        if (cli.image_a.empty()) {
            std::cerr << "[metrics] --pair --metrics requires imageA (a file in directory A)\n";
            return 3;
        }
        int cur = 0;
        std::vector<std::string> files = scan_image_directory(cli.image_a, cur);
        if (files.empty()) {
            std::cerr << "[metrics] no images found in A's directory\n";
            return 3;
        }

        int    frames = 0, missing = 0;
        double sum_psnr = 0, sum_ssim = 0, sum_flip = 0;
        int    cnt_psnr = 0, cnt_ssim = 0;

        for (const auto& apath : files) {
            std::string base  = fs::path(apath).filename().string();
            std::string bpath = (fs::path(pair_dir_b) / base).string();

            if (!fs::is_regular_file(bpath)) {
                std::cout << placeholder_row(base, "missing") << '\n';
                ++missing;
                continue;
            }
            ImageEntry A, B;
            if (!decode_image_cpu(apath, A) || !decode_image_cpu(bpath, B)) {
                std::cout << placeholder_row(base, "decode_error") << '\n';
                continue;
            }
            Metrics m = compute_pair_metrics(A, B);
            print_row(base, m);
            if (m.ok && !m.mismatch) {
                ++frames;
                if (std::isfinite(m.psnr)) { sum_psnr += m.psnr; ++cnt_psnr; }
                sum_ssim += m.ssim; ++cnt_ssim;
                sum_flip += m.flip;
            }
        }

        // Summary → stderr so stdout stays pure CSV (redirectable to a .csv file).
        std::cerr << "[metrics] frames=" << frames << " missing=" << missing;
        if (cnt_psnr > 0) std::cerr << " mean_psnr=" << fmtv(sum_psnr / cnt_psnr) << "dB";
        if (cnt_ssim > 0) std::cerr << " mean_ssim=" << fmtv(sum_ssim / cnt_ssim);
        if (frames  > 0)  std::cerr << " mean_flip=" << fmtv(sum_flip / frames);
        std::cerr << "\n";
        return 0;
    }

    // ── Single pair ──
    if (cli.image_a.empty() || cli.image_b.empty()) {
        std::cerr << "[metrics] --metrics requires two images: av --metrics A B\n";
        return 3;
    }
    ImageEntry A, B;
    if (!decode_image_cpu(cli.image_a, A)) return 4;
    if (!decode_image_cpu(cli.image_b, B)) return 4;

    Metrics m = compute_pair_metrics(A, B);
    print_row(fs::path(cli.image_a).filename().string(), m);
    if (m.mismatch) {
        std::cerr << "[metrics] size/format mismatch: "
                  << A.width << "x" << A.height << " vs "
                  << B.width << "x" << B.height << "\n";
        return 5;
    }
    return 0;
}
