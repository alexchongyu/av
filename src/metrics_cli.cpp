#include "metrics_cli.h"

#include "app.h"           // ImageEntry, CliOptions
#include "image_loader.h"  // scan_image_directory
#include "chart_export.h"  // compute_diff_stats, DiffExtraStats
#include "diff_engine.h"   // compute_ssim, SSIMResult
#include "flip_engine.h"   // compute_flip, FLIPResult
#include "image_save.h"    // compute_diff_cpu

#include <stb_image.h>
#include <stb_image_write.h>
#include <cstring>

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

// ── CI gate + aggregates + JSON/JUnit ─────────────────────────────────────────

struct Row {
    std::string name;
    Metrics     m;
    std::string status;   // "" = ok, else "missing"/"decode_error"/"mismatch"
    std::string verdict;  // "PASS"/"WARN"/"FAIL"/"" (no gate)
};

bool gates_active(const CliOptions& c) {
    return c.fail_psnr >= 0 || c.warn_psnr >= 0 || c.fail_ssim >= 0 ||
           c.fail_flip >= 0 || c.fail_maxerr >= 0;
}
// psnr/ssim: FAIL if below threshold. flip/maxerr: FAIL if above. warn_psnr → WARN.
std::string gate_verdict(const CliOptions& c, const Metrics& m) {
    bool fail = false, warn = false;
    if (c.fail_psnr   >= 0 && std::isfinite(m.psnr) && m.psnr   < c.fail_psnr)   fail = true;
    if (c.fail_ssim   >= 0 && m.ssim   < c.fail_ssim)   fail = true;
    if (c.fail_flip   >= 0 && m.flip   > c.fail_flip)   fail = true;
    if (c.fail_maxerr >= 0 && m.maxerr > c.fail_maxerr) fail = true;
    if (c.warn_psnr   >= 0 && std::isfinite(m.psnr) && m.psnr   < c.warn_psnr)   warn = true;
    return fail ? "FAIL" : (warn ? "WARN" : "PASS");
}

struct Agg { double mean=0, median=0, p95=0, min=0, max=0; int n=0; };
Agg aggregate(std::vector<double> v) {
    Agg a; a.n = (int)v.size();
    if (v.empty()) return a;
    double s = 0; for (double x : v) s += x;
    a.mean = s / v.size();
    std::sort(v.begin(), v.end());
    a.min = v.front(); a.max = v.back();
    auto q = [&](double p) {
        double idx = p * (v.size() - 1); size_t lo = (size_t)idx; double f = idx - lo;
        return (lo + 1 < v.size()) ? v[lo]*(1-f) + v[lo+1]*f : v[lo];
    };
    a.median = q(0.5); a.p95 = q(0.95);
    return a;
}

std::string jnum(double v) {  // JSON number; inf → null
    if (std::isinf(v)) return "null";
    char b[32]; std::snprintf(b, sizeof(b), "%.6g", v); return b;
}
std::string jesc(const std::string& s) {  // JSON string escape
    std::string o; for (char c : s) { if (c=='"'||c=='\\') o += '\\'; o += c; } return o;
}
std::string xesc(const std::string& s) {  // XML escape
    std::string o; for (char c : s) {
        switch (c) { case '&': o+="&amp;"; break; case '<': o+="&lt;"; break;
                     case '>': o+="&gt;"; break; case '"': o+="&quot;"; break; default: o+=c; }
    } return o;
}

void emit_json(const std::vector<Row>& rows, const Agg& ap, const Agg& as, const Agg& af,
               int nok, int nmiss, int nfail, int nwarn, bool gate) {
    std::cout << "{\n  \"frames\": [\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::cout << "    {\"file\":\"" << jesc(r.name) << "\"";
        if (r.status.empty()) {
            std::cout << ",\"width\":" << r.m.w << ",\"height\":" << r.m.h
                      << ",\"psnr_db\":" << jnum(r.m.psnr) << ",\"psnr_r\":" << jnum(r.m.psnr_r)
                      << ",\"psnr_g\":" << jnum(r.m.psnr_g) << ",\"psnr_b\":" << jnum(r.m.psnr_b)
                      << ",\"psnr_y\":" << jnum(r.m.psnr_y) << ",\"ssim\":" << jnum(r.m.ssim)
                      << ",\"flip\":" << jnum(r.m.flip) << ",\"mse\":" << jnum(r.m.mse)
                      << ",\"mae\":" << jnum(r.m.mae) << ",\"max_error\":" << jnum(r.m.maxerr)
                      << ",\"msigned\":" << jnum(r.m.msigned);
            if (!r.verdict.empty()) std::cout << ",\"verdict\":\"" << r.verdict << "\"";
        } else {
            std::cout << ",\"status\":\"" << r.status << "\"";
        }
        std::cout << "}" << (i + 1 < rows.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n  \"summary\": {\"frames\":" << nok << ",\"missing\":" << nmiss;
    auto aj = [&](const char* nm, const Agg& a) {
        if (a.n > 0) std::cout << ",\"" << nm << "\":{\"mean\":" << jnum(a.mean)
            << ",\"median\":" << jnum(a.median) << ",\"p95\":" << jnum(a.p95)
            << ",\"min\":" << jnum(a.min) << ",\"max\":" << jnum(a.max) << "}";
    };
    aj("psnr", ap); aj("ssim", as); aj("flip", af);
    if (gate) std::cout << ",\"gate\":{\"verdict\":\"" << (nfail>0?"FAIL":(nwarn>0?"WARN":"PASS"))
                        << "\",\"fail\":" << nfail << ",\"warn\":" << nwarn << "}";
    std::cout << "}\n}\n";
}

void emit_junit(const std::vector<Row>& rows) {
    int tests = 0, failures = 0, skipped = 0;
    for (const Row& r : rows) {
        if (r.status.empty() && r.m.ok) { ++tests; if (r.verdict == "FAIL") ++failures; }
        else ++skipped;
    }
    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    std::cout << "<testsuite name=\"av-metrics\" tests=\"" << tests << "\" failures=\""
              << failures << "\" skipped=\"" << skipped << "\">\n";
    for (const Row& r : rows) {
        std::cout << "  <testcase name=\"" << xesc(r.name) << "\">";
        if (!r.status.empty()) std::cout << "<skipped message=\"" << r.status << "\"/>";
        else if (r.verdict == "FAIL")
            std::cout << "<failure message=\"metric gate\">psnr=" << jnum(r.m.psnr)
                      << " ssim=" << jnum(r.m.ssim) << " flip=" << jnum(r.m.flip)
                      << " maxerr=" << jnum(r.m.maxerr) << "</failure>";
        std::cout << "</testcase>\n";
    }
    std::cout << "</testsuite>\n";
}

} // namespace

int run_metrics_headless(const CliOptions& cli, const std::string& pair_dir_b) {
    const bool gate = gates_active(cli);
    std::vector<Row> rows;

    // ── Collect rows (single or whole --pair sequence) ────────────────────────
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
        for (const auto& apath : files) {
            Row r;
            r.name = fs::path(apath).filename().string();
            std::string bpath = (fs::path(pair_dir_b) / r.name).string();
            if (!fs::is_regular_file(bpath)) { r.status = "missing"; rows.push_back(r); continue; }
            ImageEntry A, B;
            if (!decode_image_cpu(apath, A) || !decode_image_cpu(bpath, B)) {
                r.status = "decode_error"; rows.push_back(r); continue;
            }
            r.m = compute_pair_metrics(A, B);
            if (r.m.mismatch)      r.status  = "mismatch";
            else if (gate)         r.verdict = gate_verdict(cli, r.m);
            rows.push_back(r);
        }
    } else {
        if (cli.image_a.empty() || cli.image_b.empty()) {
            std::cerr << "[metrics] --metrics requires two images: av --metrics A B\n";
            return 3;
        }
        ImageEntry A, B;
        if (!decode_image_cpu(cli.image_a, A)) return 4;
        if (!decode_image_cpu(cli.image_b, B)) return 4;
        Row r;
        r.name = fs::path(cli.image_a).filename().string();
        r.m = compute_pair_metrics(A, B);
        if (r.m.mismatch)      r.status  = "mismatch";
        else if (gate)         r.verdict = gate_verdict(cli, r.m);
        rows.push_back(r);
    }

    // ── Aggregates over OK rows ───────────────────────────────────────────────
    std::vector<double> vp, vs, vf;
    int nok = 0, nmiss = 0, nfail = 0, nwarn = 0;
    for (const Row& r : rows) {
        if (r.status.empty() && r.m.ok) {
            ++nok;
            if (std::isfinite(r.m.psnr)) vp.push_back(r.m.psnr);
            vs.push_back(r.m.ssim); vf.push_back(r.m.flip);
            if      (r.verdict == "FAIL") ++nfail;
            else if (r.verdict == "WARN") ++nwarn;
        } else if (r.status == "missing") ++nmiss;
    }
    Agg ap = aggregate(vp), as = aggregate(vs), af = aggregate(vf);

    // ── Emit in the requested format ──────────────────────────────────────────
    if (cli.out_format == "json") {
        emit_json(rows, ap, as, af, nok, nmiss, nfail, nwarn, gate);
    } else if (cli.out_format == "junit") {
        emit_junit(rows);
    } else {  // csv (default): stdout = pure CSV, summary → stderr
        print_header();
        for (const Row& r : rows) {
            if (!r.status.empty()) std::cout << placeholder_row(r.name, r.status.c_str()) << '\n';
            else                   print_row(r.name, r.m);
        }
        std::cerr << "[metrics] frames=" << nok << " missing=" << nmiss;
        if (ap.n > 0) std::cerr << " mean_psnr=" << fmtv(ap.mean) << "dB median="
                                << fmtv(ap.median) << " p95=" << fmtv(ap.p95)
                                << " min=" << fmtv(ap.min);
        if (as.n > 0) std::cerr << " mean_ssim=" << fmtv(as.mean);
        if (af.n > 0) std::cerr << " mean_flip=" << fmtv(af.mean);
        if (gate) {
            std::cerr << " | GATE " << (nfail>0?"FAIL":(nwarn>0?"WARN":"PASS"))
                      << " fail=" << nfail << " warn=" << nwarn;
        }
        std::cerr << "\n";
        if (gate && (nfail > 0 || nwarn > 0)) {
            std::cerr << "[metrics]";
            for (const Row& r : rows)
                if (r.verdict == "FAIL" || r.verdict == "WARN")
                    std::cerr << " " << r.verdict << ":" << r.name;
            std::cerr << "\n";
        }
    }

    // ── Exit code ─────────────────────────────────────────────────────────────
    if (!cli.pair && rows.size() == 1 && rows[0].status == "mismatch") {
        if (cli.out_format == "csv")
            std::cerr << "[metrics] size/format mismatch\n";
        return 5;
    }
    if (gate && nfail > 0) return 10;
    return 0;
}

// ─── Headless diff-image export (--diff-out) ──────────────────────────────────

// Build an 8-bit RGBA view of an entry (u8 direct, or HDR float clamped [0,1]).
static std::vector<uint8_t> entry_to_rgba8(const ImageEntry& e, int w, int h) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 255);
    size_t n = static_cast<size_t>(w) * h * 4;
    if (!e.pixels.empty()) {
        std::memcpy(out.data(), e.pixels.data(), std::min(n, e.pixels.size()));
    } else if (!e.pixels_f32.empty()) {
        for (size_t i = 0; i < n && i < e.pixels_f32.size(); ++i)
            out[i] = static_cast<uint8_t>(std::clamp(e.pixels_f32[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return out;
}

int run_diff_out_headless(const CliOptions& cli, const std::string& /*pair_dir_b*/) {
    if (cli.image_a.empty() || cli.image_b.empty()) {
        std::cerr << "[diff-out] requires two images: av --diff-out out.png A B\n";
        return 3;
    }
    ImageEntry A, B;
    if (!decode_image_cpu(cli.image_a, A)) return 4;
    if (!decode_image_cpu(cli.image_b, B)) return 4;
    if (A.width != B.width || A.height != B.height) {
        std::cerr << "[diff-out] size mismatch: " << A.width << "x" << A.height
                  << " vs " << B.width << "x" << B.height << "\n";
        return 5;
    }
    int w = A.width, h = A.height;

    // Diff RGBA8 buffer (FLIP → magma heatmap; else CPU diff shader mirror)
    std::vector<uint8_t> diff;
    if (cli.diff_mode == DiffState::Mode::FLIP) {
        FLIPResult f = compute_flip(A, B);
        if (!f.success) { std::cerr << "[diff-out] FLIP compute failed\n"; return 6; }
        diff = std::move(f.rgba); w = f.w; h = f.h;
    } else {
        DiffState d;
        d.mode    = cli.diff_mode;   // None → PixelAbsolute inside compute_diff_cpu
        d.amplify = cli.amplify;
        diff = compute_diff_cpu(A, B, d);
    }

    const std::vector<uint8_t>* out = &diff;
    int ow = w, oh = h;
    std::vector<uint8_t> composite;
    if (cli.diff_out_sbs) {
        std::vector<uint8_t> a8 = entry_to_rgba8(A, w, h);
        std::vector<uint8_t> b8 = entry_to_rgba8(B, w, h);
        ow = w * 3; oh = h;
        composite.assign(static_cast<size_t>(ow) * oh * 4, 0);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&composite[(static_cast<size_t>(y)*ow + 0    )*4], &a8  [static_cast<size_t>(y)*w*4], static_cast<size_t>(w)*4);
            std::memcpy(&composite[(static_cast<size_t>(y)*ow + w    )*4], &diff[static_cast<size_t>(y)*w*4], static_cast<size_t>(w)*4);
            std::memcpy(&composite[(static_cast<size_t>(y)*ow + 2*w  )*4], &b8  [static_cast<size_t>(y)*w*4], static_cast<size_t>(w)*4);
        }
        out = &composite;
    }

    if (!stbi_write_png(cli.diff_out.c_str(), ow, oh, 4, out->data(), ow * 4)) {
        std::cerr << "[diff-out] failed to write " << cli.diff_out << "\n";
        return 7;
    }
    std::cerr << "[diff-out] wrote " << cli.diff_out << " (" << ow << "x" << oh << ")\n";
    return 0;
}
