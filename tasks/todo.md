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

## Phase 2 — 블링크 비교기 ✅
- [x] `BlinkState` 추가 (app.h)
- [x] `,` 토글 + `<`/`>` 간격 + Esc 종료 (app.cpp)
- [x] 메인루프 카운트다운 (main.cpp)
- [x] 최우선 레이아웃 분기 (main_window.cpp)
- [x] 상태바 인디케이터 (statusbar.cpp) + 핫키표
- [x] 빌드 성공 + 스모크 기동(크래시 없음). 실제 점멸 시각확인=사용자
- [x] 커밋

## Phase 3 — FLIP diff 모드 (정식 ꟻLIP-LDR) ✅
- [x] 레퍼런스(NVlabs/flip) WebFetch로 상수/필터/파이프라인 verbatim 확정
- [x] 신규 `flip_engine.h/.cpp`: `compute_flip()`/`compute_flip_impl()` + `FLIPComputer` + magma 다항식
- [x] `app.h` enum/상태블록/kDiffModes 행 (Ctrl+0)
- [x] main_window.cpp statics/트리거/업로드(RGBA8 magma 직접)
- [x] image_panel.cpp 디스패치(GPU+SW)/`render_heatmap_software` 일반화/픽셀리드아웃
- [x] statusbar.cpp FLIP 점수 + app.cpp 핫키(Ctrl+0)/help/CLI 텍스트
- [x] image_save.cpp FLIP→abs 폴백, CMakeLists AV_SOURCES
- [x] (보너스) `--metrics`에 flip 열 + mean_flip 요약
- [x] 빌드 + 검증: identical→0, 단조성, **NVIDIA 레퍼런스와 소수5자리 일치**, GUI 스모크 무크래시, SSIM/abs/falsecolor 무회귀
- [x] 커밋

## Review

3개 기능 모두 구현·빌드·검증 완료. phase별 원자적 커밋(푸시 대기).

- **#1 `--metrics`** (87feb4a): 헤드리스 PSNR/SSIM/(FLIP)/MSE/MAE CSV. 완전 자동 검증 — identical→inf/1/0, 단조, --pair 시퀀스, GUI 값과 동일 함수 재사용.
- **#2 블링크** (90f75a0): `,` 토글, `<`/`>` 간격, sync 강제로 정렬 보장. 빌드+스모크. 최종 점멸 시각확인=사용자.
- **#3 FLIP** (이번 커밋): 정식 ꟻLIP-LDR 충실 포팅. **NVIDIA `flip` 레퍼런스와 5자리 일치**로 정확성 입증. GUI는 SSIM 아키텍처 미러(비동기+magma 히트맵). 최종 히트맵 시각확인=사용자.

**검증 한계**: 블링크 점멸·FLIP magma 히트맵의 최종 육안 확인은 디스플레이 필요(#1·FLIP 점수는 헤드리스로 완전 검증). 3플랫폼 재빌드/설치는 별도 요청 시.
**미완/대기**: 3개 feature 커밋 모두 origin 미푸시 — 푸시 명시 승인 대기.
