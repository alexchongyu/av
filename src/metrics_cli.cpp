#include "metrics_cli.h"

#include "app.h"           // ImageEntry, CliOptions
#include "image_loader.h"  // scan_image_directory
#include "chart_export.h"  // compute_diff_stats, DiffExtraStats
#include "color.h"         // colorimetry (--probe, --uniformity)
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
    double psnr_cb = 0, psnr_cr = 0;                        // Rec.709 chroma PSNR (Cb/Cr)
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
    m.psnr_cb = ex.psnr_cb; m.psnr_cr = ex.psnr_cr;
    m.msigned = 0.2126*ex.mean_signed[0] + 0.7152*ex.mean_signed[1] + 0.0722*ex.mean_signed[2];

    SSIMResult s = compute_ssim(A, B);
    m.ssim = s.success ? s.score : 0.0;

    FLIPResult f = compute_flip(A, B);
    m.flip = f.success ? f.score : 0.0;

    m.ok   = true;
    return m;
}

// CSV columns (16): file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr
constexpr const char* CSV_HEADER =
    "file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr";
// Placeholder row for missing/mismatch/decode_error: <name>,,,<tag>, then 12 empty fields.
static std::string placeholder_row(const std::string& name, const char* tag) {
    return name + ",,," + tag + ",,,,,,,,,,,,";
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
              << fmtv(m.maxerr) << ',' << fmtv(m.msigned) << ','
              << fmtv(m.psnr_cb) << ',' << fmtv(m.psnr_cr) << '\n';
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
                      << ",\"msigned\":" << jnum(r.m.msigned)
                      << ",\"psnr_cb\":" << jnum(r.m.psnr_cb) << ",\"psnr_cr\":" << jnum(r.m.psnr_cr);
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

// ─── Headless bad-pixel validator (--validate) ────────────────────────────────

int run_validate_headless(const CliOptions& cli) {
    if (cli.image_a.empty()) {
        std::cerr << "[validate] requires an image: av --validate IMG\n";
        return 3;
    }
    ImageEntry e;
    if (!decode_image_cpu(cli.image_a, e)) return 4;

    std::cout << "class,count\n";
    if (e.pixels_f32.empty()) {
        // 8-bit image: NaN/Inf/negative/>1 are impossible by construction.
        std::cout << "nan,0\ninf,0\nnegative,0\nsuperwhite,0\n";
        std::cerr << "[validate] " << cli.image_a
                  << ": 8-bit image (no float data) — nothing to validate\n";
        return 0;
    }

    long long nan_n = 0, inf_n = 0, neg_n = 0, hi_n = 0;
    const int w = e.width, h = e.height;
    std::vector<std::string> first;   // first offenders "class,x,y,channel"
    const int MAXC = 20;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                float v = e.pixels_f32[(static_cast<size_t>(y)*w + x)*4 + c];
                const char* cls = nullptr;
                if      (std::isnan(v)) { ++nan_n; cls = "nan"; }
                else if (std::isinf(v)) { ++inf_n; cls = "inf"; }
                else if (v < 0.0f)      { ++neg_n; cls = "negative"; }
                else if (v > 1.0f)      { ++hi_n;  cls = "superwhite"; }
                if (cls && (int)first.size() < MAXC)
                    first.push_back(std::string(cls) + "," + std::to_string(x) + "," +
                                    std::to_string(y) + "," + std::string(1, "RGB"[c]));
            }

    std::cout << "nan," << nan_n << "\ninf," << inf_n
              << "\nnegative," << neg_n << "\nsuperwhite," << hi_n << "\n";
    if (!first.empty()) {
        std::cout << "# first offenders: class,x,y,channel\n";
        for (const auto& s : first) std::cout << "# " << s << "\n";
    }
    std::cerr << "[validate] " << cli.image_a << " " << w << "x" << h
              << " nan=" << nan_n << " inf=" << inf_n
              << " negative=" << neg_n << " superwhite=" << hi_n << "\n";
    return (nan_n > 0 || inf_n > 0) ? 8 : 0;   // NaN/Inf = hard fail
}

// ─── Headless colorimetry probe (--probe "X,Y") ───────────────────────────────

namespace {

// Sample pixel (px,py) of an entry as CIE XYZ (D65). LDR u8 → sRGB display value
// (gamma-encoded); HDR f32 → treated as linear light. False if out of range.
bool probe_xyz(const ImageEntry& e, int px, int py, color::XYZ& out) {
    if (px < 0 || py < 0 || px >= e.width || py >= e.height) return false;
    size_t idx = (static_cast<size_t>(py) * e.width + px) * 4;
    if (!e.pixels_f32.empty()) {
        out = color::lin_rgb_to_xyz(e.pixels_f32[idx], e.pixels_f32[idx+1], e.pixels_f32[idx+2]);
    } else if (!e.pixels.empty()) {
        out = color::srgb_to_xyz(e.pixels[idx]/255.0, e.pixels[idx+1]/255.0, e.pixels[idx+2]/255.0);
    } else return false;
    return true;
}

// One CSV colorimetry row: which,X,Y,Z,x,y,u',v',CCT,Duv,L*.
void probe_row(const char* which, const color::XYZ& c) {
    double x, y, up, vp;
    color::xyz_to_xy(c, x, y);
    color::xyz_to_upvp(c, up, vp);
    color::Lab lab = color::xyz_to_lab(c);
    std::cout << which << ',' << fmtv(c.X) << ',' << fmtv(c.Y) << ',' << fmtv(c.Z) << ','
              << fmtv(x) << ',' << fmtv(y) << ',' << fmtv(up) << ',' << fmtv(vp) << ','
              << fmtv(color::cct_mccamy(x, y)) << ',' << fmtv(color::duv_from_upvp(up, vp)) << ','
              << fmtv(lab.L) << '\n';
}

} // namespace

int run_probe_headless(const CliOptions& cli) {
    if (cli.image_a.empty()) {
        std::cerr << "[probe] --probe requires an image: av --probe X,Y A [B]\n";
        return 3;
    }
    // Parse "X,Y".
    int px = 0, py = 0;
    {
        const std::string& s = cli.probe;
        auto comma = s.find(',');
        if (comma == std::string::npos) {
            std::cerr << "[probe] --probe expects \"X,Y\" (got: " << s << ")\n";
            return 3;
        }
        try {
            px = std::stoi(s.substr(0, comma));
            py = std::stoi(s.substr(comma + 1));
        } catch (...) {
            std::cerr << "[probe] --probe expects integer \"X,Y\" (got: " << s << ")\n";
            return 3;
        }
    }

    ImageEntry A;
    if (!decode_image_cpu(cli.image_a, A)) return 4;
    color::XYZ ca;
    if (!probe_xyz(A, px, py, ca)) {
        std::cerr << "[probe] coord " << px << "," << py << " out of range (A is "
                  << A.width << "x" << A.height << ")\n";
        return 3;
    }

    std::cout << "which,X,Y,Z,x,y,uprime,vprime,CCT,Duv,Lstar\n";
    probe_row("A", ca);
    if (cli.image_b.empty()) return 0;

    ImageEntry B;
    if (!decode_image_cpu(cli.image_b, B)) return 4;
    color::XYZ cb;
    if (!probe_xyz(B, px, py, cb)) {
        std::cerr << "[probe] coord " << px << "," << py << " out of range (B is "
                  << B.width << "x" << B.height << ")\n";
        return 3;
    }
    probe_row("B", cb);

    // Delta row (A→B): CCT col = ΔCCT, Duv col = Δu'v' (chromaticity distance),
    // L* col = ΔE76; XYZ/xy/u'v' columns left blank.
    double ax, ay, aup, avp; color::xyz_to_xy(ca, ax, ay); color::xyz_to_upvp(ca, aup, avp);
    double bx, by, bup, bvp; color::xyz_to_xy(cb, bx, by); color::xyz_to_upvp(cb, bup, bvp);
    double dcct  = color::cct_mccamy(bx, by) - color::cct_mccamy(ax, ay);
    double dupvp = std::sqrt((bup-aup)*(bup-aup) + (bvp-avp)*(bvp-avp));
    double de76  = color::delta_e76(color::xyz_to_lab(ca), color::xyz_to_lab(cb));
    std::cout << "delta,,,,,,,," << fmtv(dcct) << ',' << fmtv(dupvp) << ',' << fmtv(de76) << '\n';
    return 0;
}

// ─── Headless uniformity pack (--uniformity) ──────────────────────────────────

namespace {

struct UniMetrics {
    bool   ok = false;
    int    w = 0, h = 0;
    double Lmean = 0, uni9 = 0, uni13 = 0, uni25 = 0, cv = 0, duv_max = 0, semu = 0;
};

// CIE Y at pixel i. LDR u8 → sRGB display value; HDR f32 → linear light.
double pixel_Y(const ImageEntry& e, size_t i) {
    if (!e.pixels_f32.empty())
        return color::luminance_linear(e.pixels_f32[i*4], e.pixels_f32[i*4+1], e.pixels_f32[i*4+2]);
    return color::luminance_srgb(e.pixels[i*4]/255.0, e.pixels[i*4+1]/255.0, e.pixels[i*4+2]/255.0);
}
color::XYZ pixel_XYZ(const ImageEntry& e, size_t i) {
    if (!e.pixels_f32.empty())
        return color::lin_rgb_to_xyz(e.pixels_f32[i*4], e.pixels_f32[i*4+1], e.pixels_f32[i*4+2]);
    return color::srgb_to_xyz(e.pixels[i*4]/255.0, e.pixels[i*4+1]/255.0, e.pixels[i*4+2]/255.0);
}

// Compute the uniformity pack for one decoded image.
UniMetrics compute_uniformity(const ImageEntry& e) {
    UniMetrics m;
    int w = e.width, h = e.height;
    size_t npix = static_cast<size_t>(w) * h;
    if (npix == 0 || (e.pixels.empty() && e.pixels_f32.empty())) return m;
    m.w = w; m.h = h;

    // Luminance field + CV (over all pixels) + field-mean XYZ (for colour uniformity).
    std::vector<double> L(npix);
    double sumL = 0.0, sumL2 = 0.0;
    color::XYZ meanXYZ{0,0,0};
    for (size_t i = 0; i < npix; ++i) {
        double y = pixel_Y(e, i);
        L[i] = y; sumL += y; sumL2 += y*y;
        color::XYZ c = pixel_XYZ(e, i);
        meanXYZ.X += c.X; meanXYZ.Y += c.Y; meanXYZ.Z += c.Z;
    }
    double meanL = sumL / npix;
    m.Lmean = meanL;
    double var = sumL2 / npix - meanL * meanL;
    if (var < 0) var = 0;
    m.cv = (meanL > 1e-9) ? 100.0 * std::sqrt(var) / meanL : 0.0;
    meanXYZ.X /= npix; meanXYZ.Y /= npix; meanXYZ.Z /= npix;
    double field_up, field_vp; color::xyz_to_upvp(meanXYZ, field_up, field_vp);

    // Integral image of L → O(1) box averages.
    std::vector<double> ii(static_cast<size_t>(w+1) * (h+1), 0.0);
    for (int y = 0; y < h; ++y) {
        double rs = 0.0;
        for (int x = 0; x < w; ++x) {
            rs += L[static_cast<size_t>(y)*w + x];
            ii[static_cast<size_t>(y+1)*(w+1) + (x+1)] = ii[static_cast<size_t>(y)*(w+1) + (x+1)] + rs;
        }
    }
    auto box_mean = [&](int cx, int cy, int rad) -> double {
        int x0 = std::max(0, cx-rad), x1 = std::min(w-1, cx+rad);
        int y0 = std::max(0, cy-rad), y1 = std::min(h-1, cy+rad);
        double s = ii[static_cast<size_t>(y1+1)*(w+1)+(x1+1)] - ii[static_cast<size_t>(y0)*(w+1)+(x1+1)]
                 - ii[static_cast<size_t>(y1+1)*(w+1)+x0]     + ii[static_cast<size_t>(y0)*(w+1)+x0];
        double area = static_cast<double>(x1-x0+1) * (y1-y0+1);
        return area > 0 ? s / area : 0.0;
    };

    int srad = std::max(1, std::min(w, h) / 40);   // small window for point sampling

    // N-point luminance uniformity % = 100 · Lmin/Lmax over grid points.
    auto point_uni = [&](const std::vector<std::pair<double,double>>& pts) -> double {
        double mn = 1e300, mx = -1e300;
        for (auto& p : pts) {
            int px = static_cast<int>(std::lround(p.first  * (w-1)));
            int py = static_cast<int>(std::lround(p.second * (h-1)));
            double v = box_mean(px, py, srad);
            mn = std::min(mn, v); mx = std::max(mx, v);
        }
        return (mx > 1e-12) ? 100.0 * mn / mx : 100.0;
    };
    const double g3[3] = {1.0/6, 3.0/6, 5.0/6};
    const double g5[5] = {1.0/10, 3.0/10, 5.0/10, 7.0/10, 9.0/10};
    std::vector<std::pair<double,double>> p9, p13, p25;
    for (double gy : g3) for (double gx : g3) { p9.push_back({gx,gy}); p13.push_back({gx,gy}); }
    const double inner[2] = {1.0/3, 2.0/3};               // 13-pt = 3×3 + 4 inner
    for (double gy : inner) for (double gx : inner) p13.push_back({gx,gy});
    for (double gy : g5) for (double gx : g5) p25.push_back({gx,gy});
    m.uni9  = point_uni(p9);
    m.uni13 = point_uni(p13);
    m.uni25 = point_uni(p25);

    // Colour uniformity: max Δu'v' of 25 grid points vs field-mean u'v'.
    double dmax = 0.0;
    for (auto& p : p25) {
        int px = static_cast<int>(std::lround(p.first  * (w-1)));
        int py = static_cast<int>(std::lround(p.second * (h-1)));
        int x0 = std::max(0, px-srad), x1 = std::min(w-1, px+srad);
        int y0 = std::max(0, py-srad), y1 = std::min(h-1, py+srad);
        color::XYZ c{0,0,0}; int cnt = 0;
        for (int yy = y0; yy <= y1; ++yy) for (int xx = x0; xx <= x1; ++xx) {
            color::XYZ pc = pixel_XYZ(e, static_cast<size_t>(yy)*w + xx);
            c.X += pc.X; c.Y += pc.Y; c.Z += pc.Z; ++cnt;
        }
        if (cnt) { c.X /= cnt; c.Y /= cnt; c.Z /= cnt; }
        double up, vp; color::xyz_to_upvp(c, up, vp);
        dmax = std::max(dmax, std::sqrt((up-field_up)*(up-field_up) + (vp-field_vp)*(vp-field_vp)));
    }
    m.duv_max = dmax;

    // SEMU mura proxy. Background = 2nd-order polynomial surface fit (least
    // squares over {1,x,y,x²,xy,y²}) — removes smooth shading/tilt/vignetting
    // exactly and without box-blur edge artefacts, so a pure gradient leaves ~0
    // residual (shading, not mura) while a localised blob survives. Then take the
    // worst Weber contrast of the residual and apply the SEMU visibility
    // normalisation with area S in % of frame (PROXY: physical pitch/viewing
    // distance not modelled). Flat field → 0; blob → grows with contrast·area.
    {
        double ATA[6][6] = {{0}}, ATb[6] = {0};
        auto basis = [](double nx, double ny, double* b) {
            b[0] = 1.0; b[1] = nx; b[2] = ny; b[3] = nx*nx; b[4] = nx*ny; b[5] = ny*ny;
        };
        double invw = (w > 1) ? 2.0 / (w - 1) : 0.0;
        double invh = (h > 1) ? 2.0 / (h - 1) : 0.0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            double b[6]; basis(x*invw - 1.0, y*invh - 1.0, b);
            double v = L[static_cast<size_t>(y)*w + x];
            for (int r = 0; r < 6; ++r) { ATb[r] += b[r]*v; for (int c = 0; c < 6; ++c) ATA[r][c] += b[r]*b[c]; }
        }
        // Solve ATA·coef = ATb (Gaussian elimination, partial pivot).
        double coef[6] = {0};
        {
            double M[6][7];
            for (int r = 0; r < 6; ++r) { for (int c = 0; c < 6; ++c) M[r][c] = ATA[r][c]; M[r][6] = ATb[r]; }
            bool singular = false;
            for (int col = 0; col < 6; ++col) {
                int piv = col; for (int r = col+1; r < 6; ++r) if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
                if (std::fabs(M[piv][col]) < 1e-12) { singular = true; break; }
                for (int c = 0; c < 7; ++c) std::swap(M[col][c], M[piv][c]);
                for (int r = 0; r < 6; ++r) if (r != col) {
                    double f = M[r][col] / M[col][col];
                    for (int c = col; c < 7; ++c) M[r][c] -= f * M[col][c];
                }
            }
            if (!singular) for (int r = 0; r < 6; ++r) coef[r] = M[r][6] / M[r][r];
        }
        double cmax = 0.0;
        std::vector<double> cmap(npix, 0.0);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y)*w + x;
            double b[6]; basis(x*invw - 1.0, y*invh - 1.0, b);
            double bg = 0.0; for (int k = 0; k < 6; ++k) bg += coef[k]*b[k];
            double c = (bg > 1e-9) ? (L[i] - bg) / bg : 0.0;
            cmap[i] = c;
            cmax = std::max(cmax, std::fabs(c));
        }
        if (cmax > 1e-9) {
            double thr = 0.5 * cmax; size_t blob = 0;
            for (size_t i = 0; i < npix; ++i) if (std::fabs(cmap[i]) >= thr) ++blob;
            double S_pct = 100.0 * static_cast<double>(blob) / npix;
            if (S_pct > 1e-9)
                m.semu = 100.0 * cmax / (1.97 / std::pow(S_pct, 0.33) + 0.72);
        }
    }

    m.ok = true;
    return m;
}

// Emit one uniformity CSV row (11 cols); status non-empty → placeholder.
void uni_row(const std::string& file, const char* role, const UniMetrics& m,
             const char* status = nullptr) {
    if (status) { std::cout << file << ',' << role << ',' << status << ",,,,,,,,\n"; return; }
    std::cout << file << ',' << role << ',' << m.w << ',' << m.h << ','
              << fmtv(m.Lmean) << ',' << fmtv(m.uni9) << ',' << fmtv(m.uni13) << ','
              << fmtv(m.uni25) << ',' << fmtv(m.cv) << ',' << fmtv(m.duv_max) << ','
              << fmtv(m.semu) << '\n';
}

} // namespace

int run_uniformity_headless(const CliOptions& cli, const std::string& pair_dir_b) {
    const bool gate = (cli.fail_uniformity >= 0.0f || cli.fail_semu >= 0.0f);
    auto verdict = [&](const UniMetrics& m) -> bool {   // true = FAIL
        if (cli.fail_uniformity >= 0.0f && m.uni25 < cli.fail_uniformity) return true;
        if (cli.fail_semu       >= 0.0f && m.semu  > cli.fail_semu)       return true;
        return false;
    };

    // Build the (A, B?, name) work list.
    struct Job { std::string apath, bpath, aname, bname; bool have_b; };
    std::vector<Job> jobs;
    if (cli.pair) {
        if (cli.image_a.empty()) {
            std::cerr << "[uniformity] --pair --uniformity requires imageA (a file in directory A)\n";
            return 3;
        }
        int cur = 0;
        std::vector<std::string> files = scan_image_directory(cli.image_a, cur);
        if (files.empty()) { std::cerr << "[uniformity] no images found in A's directory\n"; return 3; }
        for (const auto& apath : files) {
            std::string name = fs::path(apath).filename().string();
            std::string bpath = (fs::path(pair_dir_b) / name).string();
            jobs.push_back({apath, bpath, name, name, fs::is_regular_file(bpath)});
        }
    } else {
        if (cli.image_a.empty()) {
            std::cerr << "[uniformity] requires an image: av --uniformity A [B]\n";
            return 3;
        }
        bool have_b = !cli.image_b.empty();
        jobs.push_back({cli.image_a, cli.image_b,
                        fs::path(cli.image_a).filename().string(),
                        have_b ? fs::path(cli.image_b).filename().string() : "", have_b});
    }

    std::cout << "file,role,width,height,Lmean,uni9_pct,uni13_pct,uni25_pct,nonuni_cv_pct,duv_max,semu\n";
    int nrows = 0, nfail = 0;
    for (const Job& j : jobs) {
        ImageEntry A;
        if (!decode_image_cpu(j.apath, A)) return 4;    // hard fail (mirror --metrics)
        UniMetrics ma = compute_uniformity(A);
        if (!ma.ok) { uni_row(j.aname, "A", ma, "error"); }
        else { uni_row(j.aname, "A", ma); ++nrows; if (gate && verdict(ma)) ++nfail; }

        if (!j.have_b) continue;
        ImageEntry B;
        if (!decode_image_cpu(j.bpath, B)) return 4;
        UniMetrics mb = compute_uniformity(B);
        if (!mb.ok) { uni_row(j.bname, "B", mb, "error"); continue; }
        uni_row(j.bname, "B", mb); ++nrows; if (gate && verdict(mb)) ++nfail;

        if (ma.ok) {   // Δ(B−A): +uni% / −cv / −semu = compensation improved uniformity
            UniMetrics d; d.ok = true; d.w = mb.w; d.h = mb.h;
            d.Lmean = mb.Lmean - ma.Lmean; d.uni9 = mb.uni9 - ma.uni9;
            d.uni13 = mb.uni13 - ma.uni13; d.uni25 = mb.uni25 - ma.uni25;
            d.cv = mb.cv - ma.cv; d.duv_max = mb.duv_max - ma.duv_max; d.semu = mb.semu - ma.semu;
            uni_row("delta", "B-A", d);
        }
    }

    std::cerr << "[uniformity] rows=" << nrows
              << " (SEMU is a proxy: area in % of frame, physical pitch/viewing not modelled)";
    if (gate) std::cerr << " | GATE " << (nfail > 0 ? "FAIL" : "PASS") << " fail=" << nfail;
    std::cerr << "\n";

    if (gate && nfail > 0) return 10;
    return 0;
}
