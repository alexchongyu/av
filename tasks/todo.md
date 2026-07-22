# av 신규 기능 4종 — todo (2026-07-22, 2세션)

계획: `plans/features-colorimetry-uniformity-lumachroma-batch-20260722.md` (승인됨).
phase별 원자 커밋, 푸시 명시승인 후. 이번 범위 = Mac 빌드 + 헤드리스 검증 (사용자 확정).

## Phase 1 — 컬러메트리 프로브 ✅ (43d9af0)
- [x] 신규 src/color.{h,cpp}: sRGB EOTF, RGB→XYZ(D65), xy, u'v', CCT(McCamy), Duv(Ohno), Lab/ΔE76
- [x] CMakeLists AV_SOURCES에 color.cpp
- [x] 헤드리스 --probe X,Y A [B]: XYZ/xy/u'v'/CCT/Duv/L* CSV + delta 행
- [x] GUI: Shift+V balloon 콜러메트리(A/B/Δ) + ROI stats window 콜러메트리 행
- [x] 검증: white→x0.3127/y0.3290/CCT6505/Duv0.0032, sRGB primaries 정확, warm→저CCT

## Phase 2 — 비균일도 메트릭 팩 ✅ (a13959d)
- [x] CliOptions: uniformity/fail_uniformity/fail_semu + parse + help
- [x] run_uniformity_headless: ICDM 9/13/25pt %, CV, Δu'v'_max, SEMU 프록시
- [x] SEMU 배경 = 2차 다항식 표면 피팅(경계 아티팩트 無) + 면적가정 stderr 명시
- [x] A/B/Δ 행, --pair, --fail-uniformity/--fail-semu → exit 10
- [x] 검증: flat→uni100/cv0/semu0, ramp→semu~1(vs blob36), blob→semu36, colornu→Δu'v'0.024

## Phase 3 — Luma/Chroma 분리 PSNR ✅ (4007dd6)
- [x] DiffExtraStats: mse_cb/psnr_cb/mse_cr/psnr_cr (기존 luma 루프 내 누적, 새 루프 無)
- [x] --metrics CSV 14→16열(psnr_cb/psnr_cr 끝 append, 기존 위치 불변) + JSON 키
- [x] ChannelMode::Luma(=4): shader u_channel==4 + CPU 미러 apply_channel_grid + Shift+Y 토글
- [x] 검증: 채널별오차→psnr_r>g>b, 무채색+8→psnr_cb/cr=inf, 16열 placeholder/JSON

## Phase 4 — --batch 매니페스트 드라이버 ✅ (401e3e2)
- [x] finish_rows 추출(집계+emit+게이트) → --metrics/--batch 공유(DRY)
- [x] CliOptions.batch_path + parse + help + main.cpp 디스패치
- [x] run_batch_headless: TAB 매니페스트(A\tB[\tlabel]), #주석/빈줄, stdin(-), missing/decode 상태
- [x] 검증: 라벨/basename fallback/누락/주석/stdin/json/게이트exit10 + 회귀(--metrics 정상, mismatch→5)

## 마무리
- [x] 전체 회귀 스모크(신·구 기능 exit코드, --metrics --pair, --diff-out, --validate)
- [x] 최종 재빌드 clean (v0.22-56-g401e3e2)
- [ ] Luma 표시뷰 육안 확인 (GUI, 사용자 화면 — 헤드리스 검증 불가)
- [ ] (승인 후) 푸시 / 3플랫폼 배포

## Review
- 4개 기능 모두 Mac 빌드 clean + 헤드리스 정량검증 통과, phase별 원자 커밋 완료(미푸시 4).
- 핵심 설계: 공유 컬러 코어 color.{h,cpp}를 신설(FLIP D65와 분리 → 회귀 안전 + CIE 표준 준수).
  Phase 1이 만든 코어를 Phase 2(Δu'v'/uniformity)가 재사용. finish_rows 리팩터로 batch/metrics DRY.
- 무회귀: 기존 --metrics(14열 앞부분 위치 불변), --pair, --diff-out, --validate, SSIM/FLIP 경로 모두 불변 확인.
- 검증 한계: Luma 표시뷰·balloon/ROI 콜러메트리 육안은 사용자 화면(수학은 --probe로 기계증명).
- 교훈 2건 lessons.md 기록: (1) C++ hex escape가 뒤 hex-digit 흡수, (2) 박스 저역통과 경계 클램프 아티팩트.
