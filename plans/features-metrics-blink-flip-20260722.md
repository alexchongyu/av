# av — 신규 기능 3종 구현 계획 (#1 `--metrics` / #2 블링크 / #3 FLIP)

## Context

av는 렌더/복원 이미지 품질 비교기다. 사용자가 리서치 추천 테이블에서 상위 3개를 **순서대로 전부** 구현 요청.
- **#1 `--metrics`**: 현재 모든 검증이 GUI라 헤드리스 배치·CI 평가가 불가능(프로젝트 최대 약점). CLI로 PSNR/SSIM/MSE/MAE(+#3 이후 FLIP)를 stdout(CSV)로 뽑아 스크립트화 → 향후 자동 회귀검증 기반.
- **#2 블링크 비교기**: 작은 화질 차이를 눈으로 잡는 가장 빠른 UX(현재 수동 Tab 스왑뿐).
- **#3 FLIP**: 그래픽스 업계 표준 지각 오차맵. 기존 SSIM과 동일 아키텍처(비동기 CPU + 히트맵 텍스처)로 얹음.

세 기능은 서로 독립 → **각 phase마다 빌드·검증·원자적 커밋**. 푸시는 하지 않음(명시 승인 필요).

> 플랜모드 제약상 이 파일만 편집 가능. 승인 후 구현 착수 시 이 계획을 `proj_home/plans/`로 복사하고 `tasks/todo.md`에 체크리스트를 만든다.

---

## Phase 1 — `--metrics` 헤드리스 모드  (난이도 S~M, 헤드리스 완전 검증 가능)

**목표**: `av --metrics A B` → PSNR/SSIM/MSE/MAE를 CSV로 stdout 출력 후 창 없이 종료. `av --pair --metrics A_dir/frame B_dir` → 시퀀스 전 프레임 1행씩 + 요약행.

**핵심 제약(조사 확인)**: 기존 `load_image`/`load_image_cached`/`load_image_and_populate_sequence`는 전부 GL/SDL·ImGui 컨텍스트를 요구(텍스처 업로드, `ImGui::GetTime()`). 따라서 헤드리스는 **stbi로 CPU-only 디코드**해 `ImageEntry`를 직접 구성한다(`texture_id=0`). `compute_diff_stats`·`compute_ssim_cpu`는 순수 CPU라 그대로 재사용.

**변경 파일**
- `src/app.h` — `CliOptions`(236-257)에 `bool metrics=false;` 추가.
- `src/app.cpp` — `parse_cli`(84-168) `--pair` 분기 뒤에 `--metrics` 추가; `print_help`(55-82)에 usage 라인 추가.
- `src/diff_engine.h` / `src/diff_engine.cpp` — 공개 동기 래퍼 추가:
  `SSIMResult compute_ssim(const ImageEntry& a, const ImageEntry& b);`
  구현은 로컬 `std::atomic<bool> nocancel{false}`로 익명 네임스페이스의 `compute_ssim_cpu`(234) 호출. (익명 함수를 파일 상단으로 옮기거나 전방선언 없이 같은 TU 내 공개 함수로 래핑.)
- **신규** `src/metrics_cli.h` / `src/metrics_cli.cpp` —
  - `bool decode_image_cpu(const std::string& path, ImageEntry& out);` : `load_image`(image_loader.cpp:344-388)의 디코드부만 복제 — `stbi_loadf`(.hdr→pixels_f32,is_hdr) / `stbi_load`(→pixels RGBA8), width/height/channels=4/loaded=true 세팅, 텍스처 업로드 없음. (v1은 stb 포맷+.hdr; PPM은 미지원 명시 — PPM 파서도 내부 업로드하므로 후속 과제.)
  - `int run_metrics_headless(const CliOptions& cli, const std::string& pair_dir_b);` :
    - 단일: A·B 디코드 → 치수/포맷 불일치 시 stderr 에러+exit 3. `compute_diff_stats`로 채널별 → overall **MSE**=3ch 평균, **PSNR**=`compute_info_psnr`(app.cpp:199-203)의 `psnr>0 && <999` 평균 로직 복제, **MAE**=평균, **MaxErr**=max, **SSIM**=`compute_ssim().score`.
    - `--pair`: `scan_image_directory`(CPU-only)로 A 목록, 각 A프레임의 basename을 `pair_dir_b`에 조인(=`pair_mirror_b` 미러, image_loader.cpp:509-513) → B 경로. 프레임별 1행 + 마지막 요약행(평균 PSNR/SSIM 등). B 없으면 `...,missing` 행.
    - 출력: CSV 헤더 `file,width,height,psnr_db,ssim,mse,mae,max_error`, 값은 `export_stats_csv` 스타일 `%.6g`(chart_export.cpp:817). `std::cout`(리다이렉트 시 .csv).
- `src/main.cpp` — pair 검증(200) 직후, software 판정(202) **이전**에:
  `if (cli.metrics) return run_metrics_headless(cli, pair_dir_b);`
- `CMakeLists.txt` — `AV_SOURCES`에 `src/metrics_cli.cpp` 추가.

**검증(헤드리스)**: ① `av --metrics a.png a.png` → mse=0, psnr=inf/999, ssim=1.0. ② 서로 다른 두 이미지 → 유한값·단조성. ③ `av --pair --metrics pt/fhd/001.png pt/recon` → 5행+요약. ④ 값이 GUI Image Info의 PSNR/통계창 SSIM과 일치하는지 교차확인.

---

## Phase 2 — 블링크 비교기  (난이도 S, 시각확인)

**목표**: `,` 토글 → 두 이미지가 로드된 상태에서 **단일 전체창**에 A/B를 `interval`(기본 0.5s)마다 자동 교대 표시. `<`/`>`(Shift+,/Shift+.)로 간격 0.1~2.0s 조절. 정렬은 픽셀 단위로 고정.

**설계(조사 확인)**: 레이아웃은 매 프레임 `images[].loaded`/`diff.mode`/`overlay.active`에서 파생(main_window.cpp:1412-1414). `render_single`은 `images[panel_idx]`+`views[panel_idx]` 사용(image_panel.cpp:1820-1823). **정렬 함정**: A는 views[0], B는 views[1]을 쓰므로 그대로 교대하면 두 뷰포트가 어긋남 → 블링크 ON 시 활성 뷰포트를 양쪽에 복사하고 `sync_viewports`를 강제(끌 때 복원)해 lockstep 유지.

**변경 파일**
- `src/app.h` — slideshow(341-348) 옆에 `struct BlinkState { bool active=false; float interval=0.5f; float countdown=0.0f; bool show_b=false; bool saved_sync=true; }; BlinkState blink;` 추가.
- `src/app.cpp handle_keyboard` — `case SDL_SCANCODE_COMMA:` 추가. 무수식 `,`: 양쪽 로드 시 `blink.active` 토글. ON → `views[0]=views[1]=views[active_panel]`, `saved_sync=sync_viewports`, `sync_viewports=true`, `countdown=interval`, `show_b=false`. OFF → `sync_viewports=saved_sync`. `Shift+,`/`Shift+.` → blink.active일 때 interval ∓0.1 clamp[0.1,2.0]. Esc 종료 체인(249-264)에 blink 우선 추가.
- `src/main.cpp` — 슬라이드쇼 카운트다운(481-489) 직후: `if (state.blink.active){ state.blink.countdown-=io.DeltaTime; if(countdown<=0){ show_b=!show_b; countdown=interval; } }`.
- `src/ui/main_window.cpp` — 레이아웃 체인 최상단(1433 `if(overlay_mode)` **이전**)에 최우선 분기:
  `if (state.blink.active && two_images) { /* else 단일패널(1488-1493) 복제 */ s_panel_left.render(state, state.blink.show_b?1:0, diff_renderer_, /*force_single=*/true); }`. diff.mode/overlay를 안 건드리므로 OFF 시 이전 레이아웃 자동 복원.
- `src/ui/statusbar.cpp` — 슬라이드쇼 인디케이터(154-161) 뒤에 `blink.active` 시 "◍ BLINK A/B  %.2fs" 블록(동일 idiom).
- `src/ui/main_window.cpp` 핫키표(911-913 근처) — 블링크 토글/간격 행 추가.

**검증(시각)**: 두 이미지 로드 → `,` 눌러 교대 확인, `<`/`>` 간격 변화, 상태바 표시, 다시 `,`로 원복. 정렬 유지(패닝/줌 후에도 A/B 동일 위치).

---

## Phase 3 — FLIP diff 모드 (정식 ꟻLIP-LDR)  (난이도 M~L)

**목표**: 새 diff 모드 `FLIP`. NVIDIA ꟻLIP-LDR 알고리즘을 CPU 비동기(SSIM과 동일 구조)로 계산 → **magma 히트맵** + 평균 FLIP 점수(0=동일, 높을수록 차이). SSIM처럼 diff 셰이더를 우회하고 사전계산 히트맵 텍스처로 표시.

**알고리즘(NVlabs/flip · rotoglup C++ 레퍼런스에서 상수/필터 정확 이식 — 구현 시 WebFetch)**:
1. sRGB→linear→XYZ(D65)→**YyCxCz**.
2. 채널별 **CSF 공간필터**(분리형 가우시안, ppd로 크기 결정; 기본 ppd≈67 = 0.7m·0.7m폭·4K 가정) 적용.
3. 필터 결과 → 지각 균등색공간 → **HyAB 색차** → 정규화(cmax)·거듭제곱(qc) → ΔE_color[0,1].
4. 무채(Y) 채널에서 **엣지/점 특징**(가우시안 미분필터) 검출 → 정규화 abs 차 → ΔE_feature.
5. 결합 `FLIP = ΔE_color^(1-ΔE_feature)` → 오차맵[0,1], 평균=점수.
6. **magma LUT(256)**로 RGBA8 착색.

**신규 모듈** `src/flip_engine.h` / `src/flip_engine.cpp` (diff_engine의 SSIM 구조 미러):
- `struct FLIPResult { float score; std::vector<uint8_t> rgba; int w,h; bool success; };`
- `FLIPResult compute_flip_cpu(const ImageEntry& ref, const ImageEntry& test, float ppd, std::atomic<bool>& cancel);` (공개 — `--metrics`에서도 재사용).
- `class FLIPComputer` : `SSIMComputer`(diff_engine.cpp:305-331)와 동일한 jthread/cancel/callback.
- 내부에 magma LUT + 색변환/CSF/특징필터 상수 임베드.
- **표시 일관성**: SSIM의 GPU 경로는 R32F를 red-scale로 그리는 잠재 불일치가 있음 → FLIP은 **RGBA8 magma를 GPU·SW 양쪽에 업로드**(일반 이미지 텍스처 경로 재사용)해 GPU==CPU 동일. (SSIM 자체는 회귀 위험상 손대지 않음.)

**SSIM 배선 미러(조사가 열거한 전 지점)**
- `src/app.h`: enum에 `FLIP`(45 SSIM 뒤); 상태블록 `flip_score/flip_texture_id/flip_computing/flip_pixels/flip_w,flip_h`(52-56 미러); `kDiffModes[]`에 `{FLIP,"FLIP","FLIP","Ctrl+0","flip"}` 행(85-94).
- `src/ui/main_window.cpp`: 파일static `s_flip_computer/s_flip_ready/s_flip_result`+`flip_ver_a/b`(580-583 미러); 트리거(1330-1354 미러, content_version 더티검출); 메인스레드 업로드(1278-1328 미러, **RGBA8 업로드**).
- `src/ui/image_panel.cpp`: 렌더 디스패치(2369-2417)에서 FLIP도 `is_diff_mode` 제외+가짜엔트리 `render_single`; SW 렌더(924-980) FLIP 분기; 픽셀 리드아웃(1515-1523) flip_pixels.
- `src/ui/statusbar.cpp`: FLIP 점수 표시(64-78 미러).
- `src/app.cpp`: 핫키 Ctrl+0 토글(337-341 옆; Ctrl+0 미사용 확인); usage(60)·핫키표(main_window.cpp:921,940) 텍스트 추가.
- `src/image_save.cpp`: FLIP 저장은 flip_pixels(magma) 저장 또는 abs 폴백(164-166 패턴) — v1은 flip_pixels 저장 지향.
- `CMakeLists.txt`: `AV_SOURCES`에 `src/flip_engine.cpp`.
- **보너스**: `--metrics` 출력에 `flip` 열 추가(compute_flip_cpu 공개이므로 저비용·고가치).

**검증**: ① 헤드리스 `--metrics`로 identical→FLIP≈0, 왜곡↑→점수↑ 단조성. ② (가능 시) NVIDIA `flip` 레퍼런스(pip flip-evaluator)와 pt/fhd·pt/recon 평균 비교 — 완전 비트일치는 보장 못 함(논문 알고리즘 준수), 차이는 정직히 보고. ③ GUI: Ctrl+0로 magma 히트맵 표시, 상태바 점수, 네비게이션 시 재계산.

---

## 공통 마무리
- 빌드: `/opt/homebrew/bin/cmake --build build` (Mac). 각 phase 후 빌드 통과 + 검증.
- 커밋: phase별 원자적 커밋(자율). **푸시 금지**(승인 대기). 3플랫폼 재빌드/설치는 별도 요청 시.
- 교훈/문서: 수정·함정 발생 시 `tasks/lessons.md` 갱신.
- 검증 한계: 블링크/FLIP 히트맵의 최종 시각확인은 사용자 디스플레이 필요(#1과 FLIP 점수는 헤드리스 검증).
