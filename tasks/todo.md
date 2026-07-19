# TODO — av `--pair` + PSNR(info `p`)  (2026-07-19)

계획서: `plans/av-feature-pair-psnr-20260719.md`
원칙: 무회귀. 기존 `--sync`(뷰포트 동기화)·`p`(pathfinder) 동작 보존.

## 기능 1 — `--pair` (파일명 페어링)  ✅ 커밋 7682ec6
- [x] app.h: CliOptions.pair / AppState.pair_mode, pair_dir_b, panel_missing_msg[2]
- [x] app.cpp: print_help(--pair), parse_cli(--pair), sequence_navigate(pair→panel0)
- [x] image_loader.h/.cpp: pair_mirror_b() 헬퍼 + 로드 성공 시 panel_missing_msg 리셋
- [x] main.cpp: validate_pair_args(fail-fast, 창 열기 전 검증) + startup pair 분기, else 기존 그대로
- [x] main_window.cpp: deferred open 처리에 pair 미러 호출(target==0)
- [x] image_panel.cpp: draw_empty_panel_msg 헬퍼 → GL/SW 빈 패널 모두 missing 메시지
- [x] 빌드 + CLI 검증(--help/--pair/same-dir/인자부족) + 무회귀 확인
- [x] 커밋 C1

## 기능 2 — PSNR (info 창 `p`)  ✅ 커밋 ef1286a
- [x] app.h: info_psnr_computed / info_psnr_db / info_psnr_mismatch (전용 필드)
- [x] app.cpp: #include chart_export.h; SCANCODE_P 재구성(info+양쪽로드 → PSNR, 아니면 pathfinder)
- [x] image_loader.cpp: 로드 성공 시 info_psnr_computed 리셋(stale 방지)
- [x] main_window.cpp: Image Info 창에 PSNR 줄(색 임계값, A=ref/B=cmp)
- [x] 빌드 + PSNR 산술 교차검증(Python 참조 7.66 dB == av 표시값) + 무회귀(pathfinder) 확인
- [x] 커밋 C2

## Review
- **결과**: 두 기능 구현·빌드·검증 완료. 최종 `av v0.22-25-gef1286a (updated 2026-07-19)`.
- **`--pair`**: `--sync`(뷰포트 동기화)와 이름 충돌 → 사용자 결정으로 `--pair` 신설. A(왼쪽)가
  시퀀스 구동, B는 dirB에서 같은 파일명 미러. 없으면 B 패널에 "No matching image" 안내.
  같은 dir/인자부족은 창 열기 전 fail-fast 종료. 미러 로직은 pair_mirror_b() 단일 헬퍼로 집중.
- **PSNR**: `i` 창 + 양쪽 로드 시에만 `p` 가로챔(그 외 pathfinder 보존). 기존 compute_diff_stats
  재사용(A=ref/B=cmp), diff-mode 자동 PSNR과 분리된 전용 필드. 크기/포맷 불일치=N/A, 동일=inf.
- **검증 한계**: GUI 상호작용(미러 표시/PSNR 줄/네비게이션)은 헤드리스로 구동 불가 →
  CLI·검증·산술은 헤드리스로 증명, 시각 확인은 사용자 디스플레이에서.
- **무회귀**: 비-pair 로드/네비게이션 경로 불변, pathfinder 보존, 기존 --sync 무영향.
- **미푸시**: origin/master 대비 7 커밋 앞(신규 2 + 기존 5). 푸시는 사용자 승인 대기.
