#include "color.h"

#include <cmath>

namespace color {

// ─── sRGB transfer function ──────────────────────────────────────────────────
double srgb_to_linear(double c) {
    if (c <= 0.04045) return c / 12.92;
    return std::pow((c + 0.055) / 1.055, 2.4);
}
double linear_to_srgb(double c) {
    if (c <= 0.0031308) return c * 12.92;
    return 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// ─── linear sRGB / Rec.709 → CIE XYZ (D65, Bradford-adapted) ─────────────────
// Y-row = Rec.709 luminance weights; column sums reproduce D65 white.
static XYZ matrix_lin_to_xyz(double r, double g, double b) {
    XYZ o;
    o.X = 0.4123907993 * r + 0.3575843394 * g + 0.1804807884 * b;
    o.Y = 0.2126390059 * r + 0.7151686788 * g + 0.0721923154 * b;
    o.Z = 0.0193308187 * r + 0.1191947798 * g + 0.9505321522 * b;
    return o;
}

XYZ lin_rgb_to_xyz(double r, double g, double b) {
    return matrix_lin_to_xyz(r, g, b);
}
XYZ srgb_to_xyz(double r, double g, double b) {
    return matrix_lin_to_xyz(srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b));
}

double luminance_linear(double r, double g, double b) {
    return 0.2126390059 * r + 0.7151686788 * g + 0.0721923154 * b;
}
double luminance_srgb(double r, double g, double b) {
    return luminance_linear(srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b));
}

// ─── Chromaticity ────────────────────────────────────────────────────────────
void xyz_to_xy(const XYZ& c, double& x, double& y) {
    double s = c.X + c.Y + c.Z;
    if (s <= 0.0) { x = 0.0; y = 0.0; return; }
    x = c.X / s;
    y = c.Y / s;
}
void xyz_to_upvp(const XYZ& c, double& up, double& vp) {
    double d = c.X + 15.0 * c.Y + 3.0 * c.Z;
    if (d <= 0.0) { up = 0.0; vp = 0.0; return; }
    up = 4.0 * c.X / d;
    vp = 9.0 * c.Y / d;
}

// ─── CCT (McCamy 1992) ───────────────────────────────────────────────────────
double cct_mccamy(double x, double y) {
    double denom = y - 0.1858;
    if (std::fabs(denom) < 1e-12) return 0.0;
    double n = (x - 0.3320) / denom;
    return ((-449.0 * n + 3525.0) * n - 6823.3) * n + 5520.33;
}

// ─── Duv (Ohno/Krystek polynomial), from u'v' ────────────────────────────────
double duv_from_upvp(double up, double vp) {
    // CIE 1976 u'v' → 1960 UCS (u,v): u = u', v = (2/3) v'.
    double u = up;
    double v = (2.0 / 3.0) * vp;
    double du = u - 0.292;
    double dv = v - 0.24;
    double Lfp = std::sqrt(du * du + dv * dv);
    if (Lfp < 1e-12) return 0.0;
    double a = std::atan2(dv, du);
    if (a < 0.0) a += 2.0 * M_PI;
    // 6th-order fit of the Planckian-locus radius from the reference point.
    const double k[7] = { -0.471106, 1.925865, -2.4243787, 1.5317403,
                          -0.5179722, 0.0893944, -0.00616793 };
    double Lbb = k[0] + a * (k[1] + a * (k[2] + a * (k[3] + a * (k[4] + a * (k[5] + a * k[6])))));
    return Lfp - Lbb;  // +above / −below the Planckian locus
}

// ─── CIELab (D65) + CIE76 ΔE ─────────────────────────────────────────────────
Lab xyz_to_lab(const XYZ& c) {
    // CIE 2° D65 reference white.
    const double Xn = 0.95047, Yn = 1.00000, Zn = 1.08883;
    auto f = [](double t) {
        const double d = 6.0 / 29.0;
        return (t > d * d * d) ? std::cbrt(t) : (t / (3.0 * d * d) + 4.0 / 29.0);
    };
    double fx = f(c.X / Xn), fy = f(c.Y / Yn), fz = f(c.Z / Zn);
    Lab o;
    o.L = 116.0 * fy - 16.0;
    o.a = 500.0 * (fx - fy);
    o.b = 200.0 * (fy - fz);
    return o;
}
double delta_e76(const Lab& a, const Lab& b) {
    double dL = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

} // namespace color
