# av 신규 기능 3종 — todo (2026-07-22)

계획: `plans/warm-giggling-flute.md` (승인됨). 각 phase 원자적 커밋, 푸시 금지.

## Phase 1 — `--metrics` 헤드리스 모드 ✅
- [x] `CliOptions.metrics` 추가 (app.h) + `parse_cli` `--metrics` 분기 + help 라인 (app.cpp)
- [x] `diff_engine.h/.cpp` 공개 동기 래퍼 `compute_ssim()`
- [x] 신규 `metrics_cli.h/.cpp`: `decode_image_cpu()` + `run_metrics_headless()`
- [x] `main.cpp` 헤드리스 분기 (pair 검증 뒤, SDL 이전)
- [x] `CMakeLists.txt` AV_SOURCES += metrics_cli.cpp
- [x] 빌드 + 검증: identical→inf/1/0, 상이→단조 유한값, --pair 5행+요약(stderr), 인자오류 exit3, 리다이렉트 순수 CSV
- [x] 커밋

## Phase 2 — 블링크 비교기
- [ ] `BlinkState` 추가 (app.h)
- [ ] `,` 토글 + `<`/`>` 간격 + Esc 종료 (app.cpp)
- [ ] 메인루프 카운트다운 (main.cpp)
- [ ] 최우선 레이아웃 분기 (main_window.cpp)
- [ ] 상태바 인디케이터 (statusbar.cpp) + 핫키표
- [ ] 빌드 + 시각 검증
- [ ] 커밋

## Phase 3 — FLIP diff 모드 (정식 ꟻLIP-LDR)
- [ ] 레퍼런스(NVlabs/rotoglup) WebFetch로 상수/필터 확정
- [ ] 신규 `flip_engine.h/.cpp`: `compute_flip_cpu()` + `FLIPComputer` + magma LUT
- [ ] `app.h` enum/상태블록/kDiffModes 행
- [ ] main_window.cpp statics/트리거/업로드(RGBA8)
- [ ] image_panel.cpp 디스패치/SW/픽셀리드아웃
- [ ] statusbar.cpp 점수 + app.cpp 핫키(Ctrl+0)/help
- [ ] image_save.cpp FLIP 저장, CMakeLists AV_SOURCES
- [ ] (보너스) `--metrics`에 flip 열
- [ ] 빌드 + 검증(identical→0, 단조성, 레퍼런스 비교, GUI)
- [ ] 커밋

## Review
(구현 후 작성)
