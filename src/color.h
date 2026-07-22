#pragma once

// ─── Colorimetry core (shared) ───────────────────────────────────────────────
// CIE colour-space conversions for the colorimetry probe (--probe, hover balloon,
// ROI stats) and the uniformity pack (--uniformity). Everything here is double
// precision and self-contained: no GL/SDL/ImGui, safe in headless CLI paths.
//
// White point: CIE 2° standard illuminant D65 (x=0.31270, y=0.32900), which the
// standard sRGB→XYZ (Bradford-adapted) matrix maps pure white (1,1,1) onto. This
// is deliberately independent of flip_engine.cpp's own D65 — FLIP is a verified
// reference we do not touch; colorimetry uses the CIE-standard white so xy/u'v'/
// CCT/Duv match published references.
//
// Two distinct "luma" conventions coexist in av and must not be conflated:
//   • CIE Y (photometric luminance) = matrix Y-row applied to *linear* RGB — used
//     here (probe, uniformity) because display luminance is a linear-light quantity.
//   • Rec.709 Y' (video luma) = 0.2126R'+0.7152G'+0.0722B' on *gamma-encoded* RGB —
//     used by the diff/SSIM/luma-PSNR paths and the Luma channel view.

namespace color {

struct XYZ { double X = 0.0, Y = 0.0, Z = 0.0; };
struct Lab { double L = 0.0, a = 0.0, b = 0.0; };

// sRGB electro-optical transfer function (IEC 61966-2-1), single channel [0,1].
double srgb_to_linear(double c);
double linear_to_srgb(double c);

// Convert a colour to CIE XYZ (D65). Inputs are per-channel in [0,1].
//   srgb_to_xyz : input is an sRGB *display* value (gamma-encoded) — linearised first.
//   lin_rgb_to_xyz : input is already *linear* (e.g. HDR float) — matrix only.
XYZ srgb_to_xyz(double r, double g, double b);
XYZ lin_rgb_to_xyz(double r, double g, double b);

// CIE Y (photometric luminance, relative) directly from linear/sRGB RGB [0,1].
double luminance_srgb(double r, double g, double b);   // gamma-encoded input
double luminance_linear(double r, double g, double b); // linear input

// Chromaticity.
void xyz_to_xy(const XYZ& c, double& x, double& y);       // CIE 1931 xy
void xyz_to_upvp(const XYZ& c, double& up, double& vp);   // CIE 1976 UCS u',v'

// Correlated colour temperature (McCamy 1992 cubic approximation), kelvin.
// n = (x-0.3320)/(y-0.1858); CCT = -449n³+3525n²-6823.3n+5520.33.
double cct_mccamy(double x, double y);

// Signed Duv (distance from the Planckian locus in CIE 1960 UCS), from u'v'.
// Ohno/Krystek 6th-order polynomial approximation; sign +above / −below locus.
double duv_from_upvp(double up, double vp);

// CIELab (D65) and CIE76 colour difference.
Lab  xyz_to_lab(const XYZ& c);
double delta_e76(const Lab& a, const Lab& b);

} // namespace color
