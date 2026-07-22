# av DDI-QA 기능 6종 — todo (2026-07-22)

계획: `plans/features-ddi-qa-6-20260722.md` (승인됨). phase별 원자 커밋, 푸시 명시승인 후. 이후 3플랫폼 빌드·설치.

## Phase 1 — 채널별·Luma(Y) PSNR + 평균 부호오차 (지표 토대) ✅
- [x] chart_export.h: DiffExtraStats에 mse_y/psnr_y/mae_y/max_error_y + mean_signed[3]
- [ ] chart_export.cpp compute_diff_stats: Y 1패스 + signed_sum (u8·f32)
- [ ] metrics_cli.cpp: Metrics 확장 + CSV 열(psnr_r/g/b/psnr_y/msigned) lockstep
- [ ] 빌드 + 검증(identical→inf/0, 상이 채널별·부호) + 커밋

## Phase 2 — CI 회귀 게이트 (임계·exit code·집계·JSON/JUnit)
- [ ] CliOptions: fail_*/warn_psnr/out_format
- [ ] parse_cli: --fail-*/--format + help
- [ ] run_metrics_headless: per-frame 벡터→집계, 임계 비교→exit 10, JSON/JUnit
- [ ] 빌드 + 검증(임계 위반 exit, json 파싱) + 커밋

## Phase 3 — Signed 부호차 diff 모드
- [ ] app.h enum/kDiffModes(Ctrl+1) + app.cpp 토글
- [ ] diff_engine(id 6) + shader_sources DIFF_FRAG mode6
- [ ] image_panel cpu_render_diff + image_save compute_diff_cpu 미러
- [ ] 빌드 + GUI 스모크 + 커밋

## Phase 4 — 헤드리스 --diff-out
- [ ] CliOptions.diff_out + parse + help
- [ ] metrics_cli: run_diff_out_headless (compute_diff_cpu/flip/sidebyside → PNG)
- [ ] main.cpp 분기
- [ ] 빌드 + 검증(PNG 생성) + 커밋

## Phase 5 — NaN/Inf/범위이탈 validator
- [ ] CliOptions.validate + AppState.show_bad_pixels + parse/토글
- [ ] metrics_cli: run_validate_headless (pixels_f32 스캔, exit code)
- [ ] image_panel: render_bad_pixel_overlay (GL+soft)
- [ ] 빌드 + 검증(>1 .hdr) + 커밋

## Phase 6 — 3D LUT(.cube)/ASC-CDL (표시용)
- [ ] 신규 lut.{h,cpp}: .cube/.cdl 파서 → NxNxN 그리드
- [ ] gl_texture: gl_upload_lut3d (GL_TEXTURE_3D)
- [ ] shader_sources IMAGE_FRAG: sampler3D u_lut
- [ ] image_panel render_single(GL)+cpu_render_image(trilinear)
- [ ] app.h/app.cpp: state/CLI(--lut/--cdl)/토글/업로드, CMakeLists
- [ ] 빌드 + 검증(identity 불변) + 커밋

## 마무리
- [ ] 3플랫폼(Mac/Linux/Windows) 빌드·설치·헤드리스 검증
- [ ] (승인 후) 푸시

## Review
(구현 후 작성)
