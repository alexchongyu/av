#pragma once

#include <string>

struct CliOptions;

// ─── Headless metrics (--metrics) ─────────────────────────────────────────────
// Decode image A vs B (or, with --pair, every matched frame in the sequence)
// purely on the CPU, print PSNR/SSIM/MSE/MAE as CSV to stdout, and return an
// exit code. Creates NO window / GL context / SDL video — safe to run over SSH
// or in CI. pair_dir_b is the validated B directory (only used when cli.pair).
//
// Exit codes: 0 = success, 3 = bad args / empty sequence, 4 = decode failure,
//             5 = size/format mismatch (single-pair mode).
int run_metrics_headless(const CliOptions& cli, const std::string& pair_dir_b);

// ─── Headless diff-image export (--diff-out) ──────────────────────────────────
// Write the --diff-mode diff visualization (or FLIP magma) of A vs B to a PNG and
// exit; with cli.diff_out_sbs, write an A|diff|B composite. No window/GL.
// Exit codes: 0 ok, 3 bad args, 4 decode fail, 5 size mismatch, 6/7 compute/write fail.
int run_diff_out_headless(const CliOptions& cli, const std::string& pair_dir_b);

// ─── Headless bad-pixel validator (--validate) ────────────────────────────────
// Scan a float/HDR image (cli.image_a) for NaN/Inf/negative/>1 pixels; print
// per-class counts + first offenders (CSV) to stdout. Exit 8 if any NaN/Inf,
// else 0 (3 bad args, 4 decode fail). 8-bit images report all-zero.
int run_validate_headless(const CliOptions& cli);

// ─── Headless colorimetry probe (--probe "X,Y") ───────────────────────────────
// Sample pixel (X,Y) of A (and B if given), print CIE colorimetry
// (X,Y,Z,x,y,u',v',CCT,Duv,L*) as CSV; with B, add a delta row (ΔE76/ΔCCT/Δu'v').
// Exit codes: 0 ok, 3 bad args / coord out of range, 4 decode failure.
int run_probe_headless(const CliOptions& cli);

// ─── Headless uniformity pack (--uniformity) ──────────────────────────────────
// Flat-field display uniformity of A (and B): ICDM 9/13/25-point luminance
// uniformity %, CV nonuniformity %, colour Δu'v', and a SEMU mura-index proxy.
// With B (or --pair), emits A/B/Δ rows. --fail-uniformity/--fail-semu → exit 10.
// Exit codes: 0 ok, 3 bad args, 4 decode failure, 10 CI gate fail.
int run_uniformity_headless(const CliOptions& cli, const std::string& pair_dir_b);

// ─── Headless batch metrics (--batch <file|->) ────────────────────────────────
// Run --metrics over a manifest of arbitrary A,B pairs (no same-name constraint):
// one line per pair, TAB-separated "A<TAB>B[<TAB>label]"; '#' comments and blank
// lines skipped; "-" reads the manifest from stdin. Honours --format and the
// --fail-* gates. Exit: 0 ok, 3 bad/empty/unreadable manifest, 10 CI gate fail.
int run_batch_headless(const CliOptions& cli);
