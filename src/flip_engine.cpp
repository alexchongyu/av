#include "flip_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ─── LDR-FLIP core (faithful port of NVIDIA NVlabs/flip, FLIP.h) ──────────────
// All constants below are taken verbatim from the reference implementation.

namespace {

constexpr float PI_F = 3.14159265358979f;

struct C3 { float x = 0, y = 0, z = 0; };

// D65 reference illuminant used by FLIP (and its inverse).
constexpr C3 ILLUM     = { 0.950428545f, 1.000000000f, 1.088900371f };
constexpr C3 INV_ILLUM = { 1.052156925f, 1.000000000f, 0.918357670f };

// Spatial CSF Gaussian constants: (achromatic A, chromatic RG, chromatic BY).
constexpr C3 GC_A1 = { 1.0f,    1.0f,    34.1f  };
constexpr C3 GC_B1 = { 0.0047f, 0.0053f, 0.04f  };
constexpr C3 GC_A2 = { 0.0f,    0.0f,    13.5f  };
constexpr C3 GC_B2 = { 1.0e-5f, 1.0e-5f, 0.025f };

// FLIP tuning constants.
constexpr float GQC = 0.7f;    // colour-difference exponent
constexpr float GPC = 0.4f;    // colour redistribution pivot
constexpr float GPT = 0.95f;   // colour redistribution threshold
constexpr float GW  = 0.082f;  // feature-filter width constant
constexpr float GQF = 0.5f;    // feature-difference exponent

// ── colour-space helpers ─────────────────────────────────────────────────────

inline float srgb2lin(float c) {
    return (c <= 0.04045f) ? (c / 12.92f)
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float lin2srgb(float c) {
    return (c <= 0.0031308f) ? (c * 12.92f)
                             : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

inline C3 linRGB_to_XYZ(C3 c) {
    return {
        0.4123907993f * c.x + 0.3575843394f * c.y + 0.1804807884f * c.z,
        0.2126390059f * c.x + 0.7151686788f * c.y + 0.0721923154f * c.z,
        0.0193308187f * c.x + 0.1191947798f * c.y + 0.9505321522f * c.z,
    };
}
inline C3 XYZ_to_linRGB(C3 c) {
    return {
         3.2409699419f * c.x - 1.5373831776f * c.y - 0.4986107603f * c.z,
        -0.9692436363f * c.x + 1.8759675015f * c.y + 0.0415550574f * c.z,
         0.0556300797f * c.x - 0.2039769589f * c.y + 1.0569715142f * c.z,
    };
}

inline C3 XYZ_to_YCxCz(C3 xyz) {
    xyz.x *= INV_ILLUM.x; xyz.y *= INV_ILLUM.y; xyz.z *= INV_ILLUM.z;
    return { 116.0f * xyz.y - 16.0f,
             500.0f * (xyz.x - xyz.y),
             200.0f * (xyz.y - xyz.z) };
}
inline C3 YCxCz_to_XYZ(C3 c) {
    float yy = (c.x + 16.0f) / 116.0f;
    float xx = c.y / 500.0f + yy;
    float zz = yy - c.z / 200.0f;
    return { xx * ILLUM.x, yy * ILLUM.y, zz * ILLUM.z };
}

inline C3 XYZ_to_CIELab(C3 xyz) {
    constexpr float delta  = 6.0f / 29.0f;
    constexpr float d3     = delta * delta * delta;
    constexpr float factor = 1.0f / (3.0f * delta * delta);
    constexpr float term   = 4.0f / 29.0f;
    xyz.x *= INV_ILLUM.x; xyz.y *= INV_ILLUM.y; xyz.z *= INV_ILLUM.z;
    xyz.x = (xyz.x > d3) ? std::cbrt(xyz.x) : factor * xyz.x + term;
    xyz.y = (xyz.y > d3) ? std::cbrt(xyz.y) : factor * xyz.y + term;
    xyz.z = (xyz.z > d3) ? std::cbrt(xyz.z) : factor * xyz.z + term;
    return { 116.0f * xyz.y - 16.0f,
             500.0f * (xyz.x - xyz.y),
             200.0f * (xyz.y - xyz.z) };
}

inline float hunt(float L, float c) { return 0.01f * L * c; }

inline float hyab(const C3& a, const C3& b) {
    float dl = std::fabs(a.x - b.x);
    float dy = a.y - b.y, dz = a.z - b.z;
    return dl + std::sqrt(dy * dy + dz * dz);
}

inline C3 clamp01(C3 c) {
    c.x = std::clamp(c.x, 0.0f, 1.0f);
    c.y = std::clamp(c.y, 0.0f, 1.0f);
    c.z = std::clamp(c.z, 0.0f, 1.0f);
    return c;
}

// Filtered YCxCz -> Hunt-adjusted CIELab (with in-gamut clamp), matching FLIP.
inline C3 ycxcz_to_lab_hunt(C3 ycxcz) {
    C3 lin = clamp01(XYZ_to_linRGB(YCxCz_to_XYZ(ycxcz)));
    C3 lab = XYZ_to_CIELab(linRGB_to_XYZ(lin));
    lab.y = hunt(lab.x, lab.y);
    lab.z = hunt(lab.x, lab.z);
    return lab;
}

// Maximum HyAB^GQC distance (pure green vs pure blue) used for redistribution.
float compute_cmax() {
    C3 g = XYZ_to_CIELab(linRGB_to_XYZ({0.0f, 1.0f, 0.0f}));
    C3 b = XYZ_to_CIELab(linRGB_to_XYZ({0.0f, 0.0f, 1.0f}));
    C3 gh = { g.x, hunt(g.x, g.y), hunt(g.x, g.z) };
    C3 bh = { b.x, hunt(b.x, b.y), hunt(b.x, b.z) };
    return std::pow(hyab(gh, bh), GQC);
}

inline float gaussian(float x2, float a, float b) {
    return a * std::sqrt(PI_F / b) * std::exp(-PI_F * PI_F * x2 / b);
}
inline float gaussian_sqrt(float x2, float a, float b) {
    return std::sqrt(a * std::sqrt(PI_F / b)) * std::exp(-PI_F * PI_F * x2 / b);
}

// magma colormap (Matt Zucker polynomial approximation of matplotlib magma).
inline void magma(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float c0[3] = {-0.002136485053939f, -0.000749655052795f, -0.005386127855323f};
    const float c1[3] = { 0.251660540737164f,  0.677523243683767f,  2.494026599312350f};
    const float c2[3] = { 8.353717279216625f, -3.577719514958484f,  0.314467903013257f};
    const float c3[3] = {-27.66873308576866f,  14.26473078096533f, -13.64921318813922f};
    const float c4[3] = { 52.17613981234068f, -27.94360607168351f,  12.94416944238394f};
    const float c5[3] = {-50.76852536473588f,  29.04658282127291f,   4.23415299384598f};
    const float c6[3] = { 18.65570506591883f, -11.48977351997711f,  -5.601961508734096f};
    float out[3];
    for (int i = 0; i < 3; ++i) {
        float v = c0[i] + t*(c1[i] + t*(c2[i] + t*(c3[i] + t*(c4[i] + t*(c5[i] + t*c6[i])))));
        out[i] = std::clamp(v, 0.0f, 1.0f);
    }
    r = static_cast<uint8_t>(out[0] * 255.0f + 0.5f);
    g = static_cast<uint8_t>(out[1] * 255.0f + 0.5f);
    b = static_cast<uint8_t>(out[2] * 255.0f + 0.5f);
}

// Fetch pixel (x,y) as sRGB [0,1] from an ImageEntry (u8 sRGB or clamped/sRGB-
// encoded HDR float). channels are RGBA, 4 components/pixel.
inline C3 get_srgb(const ImageEntry& e, bool use_f32, int x, int y, int w) {
    size_t idx = (static_cast<size_t>(y) * w + x) * 4;
    if (use_f32) {
        // HDR is linear → clamp to [0,1] and sRGB-encode so srgb2lin round-trips.
        return { lin2srgb(std::clamp(e.pixels_f32[idx + 0], 0.0f, 1.0f)),
                 lin2srgb(std::clamp(e.pixels_f32[idx + 1], 0.0f, 1.0f)),
                 lin2srgb(std::clamp(e.pixels_f32[idx + 2], 0.0f, 1.0f)) };
    }
    return { e.pixels[idx + 0] / 255.0f,
             e.pixels[idx + 1] / 255.0f,
             e.pixels[idx + 2] / 255.0f };
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Cancellable core: cancel may be null (synchronous path) or point at the
// worker's cancel flag (checked once per output row).
FLIPResult compute_flip_impl(const ImageEntry& reference, const ImageEntry& test,
                             float ppd, std::atomic<bool>* cancel) {
    FLIPResult res;
    if (!reference.loaded || !test.loaded) return res;

    bool use_f32 = !reference.pixels_f32.empty() && !test.pixels_f32.empty();
    bool use_u8  = !reference.pixels.empty()     && !test.pixels.empty();
    if (!use_f32 && !use_u8) return res;

    const int w = std::min(reference.width,  test.width);
    const int h = std::min(reference.height, test.height);
    if (w <= 0 || h <= 0) return res;
    const size_t N = static_cast<size_t>(w) * h;

    // ── 1. sRGB → YCxCz + achromatic luminance (for feature detection) ────────
    std::vector<C3> refYcc(N), testYcc(N);
    std::vector<float> refLum(N), testLum(N);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x;
            C3 rs = get_srgb(reference, use_f32, x, y, w);
            C3 ts = get_srgb(test,      use_f32, x, y, w);
            C3 rl = { srgb2lin(rs.x), srgb2lin(rs.y), srgb2lin(rs.z) };
            C3 tl = { srgb2lin(ts.x), srgb2lin(ts.y), srgb2lin(ts.z) };
            refYcc[i]  = XYZ_to_YCxCz(linRGB_to_XYZ(rl));
            testYcc[i] = XYZ_to_YCxCz(linRGB_to_XYZ(tl));
            refLum[i]  = (refYcc[i].x  + 16.0f) / 116.0f;   // Y/Yn ∈ [0,1]
            testLum[i] = (testYcc[i].x + 16.0f) / 116.0f;
        }
    }

    // ── 2. Build separable spatial CSF filters (Y/Cx and Cz) ─────────────────
    const float maxScale = 0.04f;  // max(b1,b2)
    const int   sRadius  = static_cast<int>(std::ceil(3.0f * std::sqrt(maxScale / (2.0f * PI_F * PI_F)) * ppd));
    const int   sWidth   = 2 * sRadius + 1;
    const float deltaX   = 1.0f / ppd;
    std::vector<float> fY(sWidth), fCx(sWidth), fCz1(sWidth), fCz2(sWidth);
    {
        float sumY = 0, sumCx = 0, sumCz1 = 0, sumCz2 = 0;
        for (int i = 0; i < sWidth; ++i) {
            float ix  = (i - sRadius) * deltaX;
            float ix2 = ix * ix;
            fY[i]   = gaussian(ix2, GC_A1.x, GC_B1.x);
            fCx[i]  = gaussian(ix2, GC_A1.y, GC_B1.y);
            fCz1[i] = gaussian_sqrt(ix2, GC_A1.z, GC_B1.z);
            fCz2[i] = gaussian_sqrt(ix2, GC_A2.z, GC_B2.z);
            sumY += fY[i]; sumCx += fCx[i]; sumCz1 += fCz1[i]; sumCz2 += fCz2[i];
        }
        float invY = 1.0f / sumY, invCx = 1.0f / sumCx;
        float invCz = 1.0f / std::sqrt(sumCz1 * sumCz1 + sumCz2 * sumCz2);
        for (int i = 0; i < sWidth; ++i) {
            fY[i] *= invY; fCx[i] *= invCx; fCz1[i] *= invCz; fCz2[i] *= invCz;
        }
    }

    // ── 3. Colour difference via separable CSF filtering + HyAB + remap ──────
    const float cmax   = compute_cmax();
    const float pccmax = GPC * cmax;

    // horizontal pass → intermediate (Y,Cx) and (Cz1,Cz2) for ref/test
    std::vector<C3> iRefYCx(N), iTestYCx(N), iRefCz(N), iTestCz(N);
    for (int y = 0; y < h; ++y) {
        if (cancel && cancel->load()) return res;
        for (int x = 0; x < w; ++x) {
            C3 aRefYCx{}, aTestYCx{}, aRefCz{}, aTestCz{};
            for (int k = -sRadius; k <= sRadius; ++k) {
                int xx = clampi(x + k, 0, w - 1);
                size_t si = static_cast<size_t>(y) * w + xx;
                int fk = k + sRadius;
                const C3& r = refYcc[si];
                const C3& t = testYcc[si];
                aRefYCx.x  += fY[fk]  * r.x;  aRefYCx.y  += fCx[fk] * r.y;
                aTestYCx.x += fY[fk]  * t.x;  aTestYCx.y += fCx[fk] * t.y;
                aRefCz.x   += fCz1[fk] * r.z; aRefCz.y   += fCz2[fk] * r.z;
                aTestCz.x  += fCz1[fk] * t.z; aTestCz.y  += fCz2[fk] * t.z;
            }
            size_t i = static_cast<size_t>(y) * w + x;
            iRefYCx[i] = aRefYCx; iTestYCx[i] = aTestYCx;
            iRefCz[i]  = aRefCz;  iTestCz[i]  = aTestCz;
        }
    }

    // vertical pass → final YCxCz → Lab-Hunt → HyAB → colour-difference map
    std::vector<float> colorDiff(N);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            C3 fRefYCx{}, fTestYCx{}, fRefCz{}, fTestCz{};
            for (int k = -sRadius; k <= sRadius; ++k) {
                int yy = clampi(y + k, 0, h - 1);
                size_t si = static_cast<size_t>(yy) * w + x;
                int fk = k + sRadius;
                fRefYCx.x  += fY[fk]  * iRefYCx[si].x;  fRefYCx.y  += fCx[fk] * iRefYCx[si].y;
                fTestYCx.x += fY[fk]  * iTestYCx[si].x; fTestYCx.y += fCx[fk] * iTestYCx[si].y;
                fRefCz.x   += fCz1[fk] * iRefCz[si].x;  fRefCz.y   += fCz2[fk] * iRefCz[si].y;
                fTestCz.x  += fCz1[fk] * iTestCz[si].x; fTestCz.y  += fCz2[fk] * iTestCz[si].y;
            }
            C3 refYCxCz  = { fRefYCx.x,  fRefYCx.y,  fRefCz.x  + fRefCz.y  };
            C3 testYCxCz = { fTestYCx.x, fTestYCx.y, fTestCz.x + fTestCz.y };
            C3 rLab = ycxcz_to_lab_hunt(refYCxCz);
            C3 tLab = ycxcz_to_lab_hunt(testYCxCz);

            float cd = std::pow(hyab(rLab, tLab), GQC);
            if (cd < pccmax) cd *= GPT / pccmax;
            else             cd  = GPT + ((cd - pccmax) / (cmax - pccmax)) * (1.0f - GPT);
            colorDiff[static_cast<size_t>(y) * w + x] = cd;
        }
    }

    // ── 4. Feature detection (edge + point) on the achromatic channel ────────
    const float featStd = 0.5f * GW * ppd;
    const int   fRadius = static_cast<int>(std::ceil(3.0f * featStd));
    const int   fWidth  = 2 * fRadius + 1;
    std::vector<float> fg(fWidth), fdg(fWidth), fddg(fWidth);
    {
        float gSum = 0, dgPos = 0, dgNeg = 0, ddgPos = 0, ddgNeg = 0;
        float s2 = featStd * featStd;
        for (int i = 0; i < fWidth; ++i) {
            int   xx = i - fRadius;
            float g  = std::exp(-(xx * xx) / (2.0f * s2));
            float dg = -static_cast<float>(xx) * g;
            float ddg = ((xx * xx) / s2 - 1.0f) * g;
            fg[i] = g; fdg[i] = dg; fddg[i] = ddg;
            gSum += g;
            if (dg  > 0.0f) dgPos  += dg;  else dgNeg  -= dg;
            if (ddg > 0.0f) ddgPos += ddg; else ddgNeg -= ddg;
        }
        for (int i = 0; i < fWidth; ++i) {
            fg[i]  /= gSum;
            fdg[i] /= (fdg[i]  > 0.0f) ? dgPos  : dgNeg;
            fddg[i]/= (fddg[i] > 0.0f) ? ddgPos : ddgNeg;
        }
    }

    // horizontal feature passes: L*g, L*dg, L*ddg for ref/test
    std::vector<float> rHg(N), rHdg(N), rHddg(N), tHg(N), tHdg(N), tHddg(N);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float rg = 0, rdg = 0, rddg = 0, tg = 0, tdg = 0, tddg = 0;
            for (int k = -fRadius; k <= fRadius; ++k) {
                int xx = clampi(x + k, 0, w - 1);
                size_t si = static_cast<size_t>(y) * w + xx;
                int fk = k + fRadius;
                float rl = refLum[si], tl = testLum[si];
                rg += fg[fk] * rl; rdg += fdg[fk] * rl; rddg += fddg[fk] * rl;
                tg += fg[fk] * tl; tdg += fdg[fk] * tl; tddg += fddg[fk] * tl;
            }
            size_t i = static_cast<size_t>(y) * w + x;
            rHg[i] = rg; rHdg[i] = rdg; rHddg[i] = rddg;
            tHg[i] = tg; tHdg[i] = tdg; tHddg[i] = tddg;
        }
    }

    // ── 5. Vertical feature passes + combine into final error map ────────────
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    std::vector<uint8_t> rgba(N * 4);
    double total = 0.0;
    for (int y = 0; y < h; ++y) {
        if (cancel && cancel->load()) return res;
        for (int x = 0; x < w; ++x) {
            float rdx = 0, rdy = 0, rddx = 0, rddy = 0;
            float tdx = 0, tdy = 0, tddx = 0, tddy = 0;
            for (int k = -fRadius; k <= fRadius; ++k) {
                int yy = clampi(y + k, 0, h - 1);
                size_t si = static_cast<size_t>(yy) * w + x;
                int fk = k + fRadius;
                // gradient x = (dg⊗x)·(g⊗y); gradient y = (g⊗x)·(dg⊗y)
                rdx  += fg[fk]  * rHdg[si];   rdy  += fdg[fk]  * rHg[si];
                rddx += fg[fk]  * rHddg[si];  rddy += fddg[fk] * rHg[si];
                tdx  += fg[fk]  * tHdg[si];   tdy  += fdg[fk]  * tHg[si];
                tddx += fg[fk]  * tHddg[si];  tddy += fddg[fk] * tHg[si];
            }
            float edgeR  = std::sqrt(rdx * rdx + rdy * rdy);
            float edgeT  = std::sqrt(tdx * tdx + tdy * tdy);
            float pointR = std::sqrt(rddx * rddx + rddy * rddy);
            float pointT = std::sqrt(tddx * tddx + tddy * tddy);
            float fd = std::pow(invSqrt2 * std::max(std::fabs(edgeR - edgeT),
                                                    std::fabs(pointR - pointT)), GQF);

            size_t i = static_cast<size_t>(y) * w + x;
            float err = std::pow(colorDiff[i], 1.0f - fd);
            err = std::clamp(err, 0.0f, 1.0f);
            total += err;

            uint8_t cr, cg, cb;
            magma(err, cr, cg, cb);
            rgba[i * 4 + 0] = cr; rgba[i * 4 + 1] = cg;
            rgba[i * 4 + 2] = cb; rgba[i * 4 + 3] = 255;
        }
    }

    res.w       = w;
    res.h       = h;
    res.rgba    = std::move(rgba);
    res.score   = static_cast<float>(total / static_cast<double>(N));
    res.success = true;
    return res;
}

} // namespace

// ─── compute_flip (public synchronous wrapper) ────────────────────────────────

FLIPResult compute_flip(const ImageEntry& reference, const ImageEntry& test, float ppd) {
    return compute_flip_impl(reference, test, ppd, nullptr);
}

// ─── FLIPComputer ─────────────────────────────────────────────────────────────

void FLIPComputer::cancel() {
    cancel_requested_.store(true);
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    running_.store(false);
    cancel_requested_.store(false);
}

void FLIPComputer::compute(const ImageEntry& a, const ImageEntry& b,
                           std::function<void(FLIPResult)> callback, float ppd) {
    cancel();

    ImageEntry a_copy = a;
    ImageEntry b_copy = b;
    a_copy.texture_id = 0;
    b_copy.texture_id = 0;

    running_.store(true);
    cancel_requested_.store(false);

    thread_ = std::jthread([this,
                            ac = std::move(a_copy),
                            bc = std::move(b_copy),
                            cb = std::move(callback),
                            ppd](std::stop_token st) {
        (void)st;
        FLIPResult result = compute_flip_impl(ac, bc, ppd, &cancel_requested_);
        running_.store(false);
        if (!cancel_requested_.load()) cb(std::move(result));
    });
}
