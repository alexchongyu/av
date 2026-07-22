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
