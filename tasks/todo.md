# TODO — av `--pair` + PSNR(info `p`)  (2026-07-19)

계획서: `plans/av-feature-pair-psnr-20260719.md`
원칙: 무회귀. 기존 `--sync`(뷰포트 동기화)·`p`(pathfinder) 동작 보존.

## 기능 1 — `--pair` (파일명 페어링)
- [ ] app.h: CliOptions.pair / AppState.pair_mode, pair_dir_b, panel_missing_msg[2]
- [ ] app.cpp: print_help(--pair), parse_cli(--pair), sequence_navigate(pair→panel0)
- [ ] image_loader.h/.cpp: pair_mirror_b() 헬퍼 + 로드 성공 시 missing/info_psnr 리셋
- [ ] main.cpp: startup pair 분기(검증·same-dir 경고+종료·A로드·pair_mirror_b), else는 기존 그대로
- [ ] main_window.cpp: deferred open 처리에 pair 미러 호출(target==0)
- [ ] image_panel.cpp: render_single / render_single_software 빈 패널에 missing 메시지
- [ ] 빌드 + CLI 검증(--help/--pair/same-dir) + 무회귀 확인
- [ ] 커밋 C1

## 기능 2 — PSNR (info 창 `p`)
- [ ] app.h: info_psnr_computed / info_psnr_db / info_psnr_mismatch
- [ ] app.cpp: #include chart_export.h; SCANCODE_P 재구성(info+양쪽로드 → PSNR, 아니면 pathfinder)
- [ ] main_window.cpp: Image Info 창에 PSNR 줄(색 임계값, A=ref/B=cmp)
- [ ] 빌드 + PSNR 산술 검증(동일→inf, 차이→손계산 대조) + 무회귀(pathfinder) 확인
- [ ] 커밋 C2

## Review
(구현 후 작성)
