# av — DDI-QA 기능 6종 구현 계획 (CI게이트 / NaN검사기 / 채널·Y지표 / 부호차맵 / --diff-out / 3D LUT)

## Context

av는 DDI(디스플레이 드라이버 IC) 보상 IP 출력(보상 vs 기준 이미지)을 비교·검증하는 도구다. 리서치 로드맵의 **top-5 + 3D LUT**를 구현해, av를 "스크립트 가능한 렌더/패널-QA 게이트"로 확장한다. 6개 기능은 모두 `app.h`·`metrics_cli`·`diff_engine`·`shader_sources` 등 공통 코어를 건드리므로 **순차 구현**(병렬 워크트리 충돌 회피), phase별 빌드·검증·원자 커밋. 이후 3플랫폼 빌드·설치. 푸시는 명시 승인 후.

조사로 확정된 사실:
- `DiffExtraStats`(chart_export.h:32) = mse/psnr/mae/max_error `[3]` 채널별 **이미 계산**. PSNR peak=255(u8)/1.0(f32).
- **Y-SSIM == 기존 SSIM** (compute_ssim는 이미 Rec.709 luma 기반, diff_engine.cpp:133-142). 새 계산 불필요.
- diff 모드 정수맵: diff_engine.cpp:50-59 (abs0/rel1/fc2/hi3/enh4/blend5). SSIM·FLIP은 히트맵 우회. **Signed는 일반 diff 셰이더 모드**(id 6).
- 헤드리스 분기 위치: main.cpp:204 (`if(cli.metrics) return ...`), SDL_Init 이전.
- `compute_diff_cpu`(image_save.cpp:155)는 abs/rel/falsecolor만 처리(highlight/enhance/blend은 abs 폴백, channel/threshold 무시). PNG writer=`save_png_impl`(image_save.cpp:60, static).
- GL 3.3 Core → `sampler3D`/`GL_TEXTURE_3D` 사용 가능. 3D 텍스처 헬퍼 없음(신규). CPU 이미지 경로=`cpu_render_image`(image_panel.cpp:127).
- pixels_f32는 HDR/고비트PPM에서만 채워짐 → NaN/Inf/음수/>1은 float 입력만 의미. 단일이미지 오버레이=ImGui draw list(모델: render_crosshair, image_panel.cpp:1261).

---

## Phase 1 — 채널별·Luma(Y) PSNR + 평균 부호오차 (지표 토대)  [S, ✅헤드리스]
#4의 부호 지표와 #3을 공유하는 `DiffExtraStats` 확장부터.
- `src/chart_export.h`: `DiffExtraStats`에 `double mse_y,psnr_y,mae_y,max_error_y;` + `double mean_signed[3];` 추가.
- `src/chart_export.cpp compute_diff_stats`(166-223): u8·f32 각 분기에 **픽셀 단위 1패스** 추가 — Y=0.2126R+0.7152G+0.0722B의 MSE/PSNR/MAE/MaxErr(peak 255/1.0, 기존 guard·+inf 재사용) + 채널별 `signed_sum += (A−B)`(abs 아님) → `mean_signed[c]=sum/npix`.
- `src/metrics_cli.cpp`: `Metrics`에 `psnr_r/g/b, psnr_y, msigned_r/g/b` 추가; `compute_pair_metrics`에서 `ex.psnr[c]`·`ex.psnr_y`·`ex.mean_signed[c]` 복사. CSV 헤더+`print_row`+mismatch/missing/decode 자리표시자 **콤마 수 lockstep** 갱신(새 열: `psnr_r,psnr_g,psnr_b,psnr_y,msigned`; ssim은 곧 Y-SSIM이므로 라벨만). 
- 검증: `--metrics A A`→psnr_*=inf, msigned=0; 상이쌍→채널별 유한값, msigned 부호(밝기 편향 방향) 확인.

## Phase 2 — CI 회귀 게이트 (임계·exit code·집계·JSON/JUnit)  [S, ✅헤드리스]
- `src/app.h CliOptions`(245): `float fail_psnr/warn_psnr/fail_ssim/fail_flip/fail_maxerr = -1;` + `std::string out_format="csv";`.
- `src/app.cpp parse_cli`(158 이전): `--fail-psnr/--warn-psnr/--fail-ssim/--fail-flip/--fail-maxerr <val>`(next()+stof), `--format csv|json|junit`; print_help(67) usage 추가.
- `src/metrics_cli.cpp run_metrics_headless`: 프레임별 `std::vector<double>` psnr/ssim/flip 수집 → 루프 후 mean/median/p95/min/max 계산(std::sort, <algorithm> 기존). 각 쌍마다 임계 비교 → 위반 시 **새 exit code 10(FAIL)/규칙상 warn은 0 유지**, stderr에 `PASS/FAIL/WARN` + 위반 프레임. `--format json`(stdout에 배열+summary 수제작)·`junit`(testsuite/testcase XML). CSV 기본 유지.
- 검증: `--pair --metrics gold recon --fail-psnr 40` → 위반 시 exit 10; `--format json` 파싱 가능; 집계 p95/min 출력.

## Phase 3 — Signed 부호차 diff 모드 (발산 히트맵)  [S, GUI+지표]
- `src/app.h`: `DiffState::Mode::Signed` enum + `kDiffModes` 행 `{Signed,"Signed","Signed","Ctrl+1","signed"}`(Ctrl+1 미사용 확인).
- `src/app.cpp handle_keyboard`(Ctrl+num 블록): `key_num==1`→Signed 토글.
- `src/diff_engine.cpp`(58): `case Signed: diff_mode_int=6;`.
- `src/shader_sources.h DIFF_FRAG_SRC`(164 앞): `else if(u_diff_mode==6)` — 채널별 `s=(a-b)*u_amplify`; 파랑(s<0)·흰(0)·빨강(s>0) 발산 매핑; 문서주석(69-73) 갱신.
- `src/ui/image_panel.cpp cpu_render_diff`(427 앞) + `src/image_save.cpp compute_diff_cpu`(192 앞): 동일 Signed 분기(soft/save 미러).
- 검증: GUI Ctrl+1 스모크(무크래시); 저장/`--diff-out`로 발산맵 PNG.

## Phase 4 — 헤드리스 diff 이미지 내보내기 `--diff-out`  [M, ✅헤드리스]
- `src/app.h CliOptions`: `std::string diff_out;`. `src/app.cpp`: `--diff-out <path>` 파싱 + help.
- `src/metrics_cli.h`: `decode_image_cpu` 공개(현재 anon static) 또는 그대로 두고 신규 함수 동일 TU에 배치. PNG writer는 `save_png_impl` de-static 또는 `stbi_write_png` 직접.
- `src/metrics_cli.cpp` 신규 `run_diff_out_headless(cli, pair_dir_b)`: A/B 디코드→`DiffState{mode=cli.diff_mode, amplify=cli.amplify}`→ `compute_diff_cpu`(abs/rel/falsecolor/**signed**) 또는 `compute_flip`(rgba) → 옵션 `sidebyside`(A│Δ│B 가로합성) → PNG. 
- `src/main.cpp`(205): `if(!cli.diff_out.empty()) return run_diff_out_headless(cli,pair_dir_b);`.
- 검증: `--diff-out d.png --diff-mode signed A B` → PNG 생성·크기·비검정 픽셀 확인; flip/sidebyside도.

## Phase 5 — NaN/Inf/범위이탈 검사기 (`--validate` + 오버레이)  [M, 헤드리스 부분]
- `src/app.h`: `CliOptions.validate` bool; `AppState.show_bad_pixels` bool. `parse_cli` `--validate`; handle_keyboard 토글 키(빈 스캔코드).
- `src/metrics_cli.cpp` 신규 `run_validate_headless(cli)`: `decode_image_cpu`로 pixels_f32 스캔 → NaN/±Inf/음수/>1 클래스별 카운트 + 첫 N 좌표 CSV, 하나라도 있으면 nonzero exit. `src/main.cpp`(205)에 분기.
- GUI: `src/ui/image_panel.cpp` 신규 `render_bad_pixel_overlay` — render_single(1890 뒤)·render_single_software에서 호출, 뷰포트 내 플래그 픽셀에 키색(NaN=마젠타/Inf=빨강/음수=파랑/>1=주황) `AddRectFilled`(역변환 L1415-1418 모델).
- **한계**: pixels_f32 있는 float/HDR 입력만 유효(8bit는 해당 없음). 헤드리스 검증은 `>1` 클래스를 super-white .hdr로 확인; NaN/Inf/음수는 EXR 부재로 합성 곤란 → 코드리뷰 + >1 경로로 기계 입증(정직 명시).

## Phase 6 — 3D LUT(.cube)/ASC-CDL 적용 (표시용, 양 패널)  [L, GUI]
- 신규 `src/lut.{h,cpp}`: `.cube`(1D/3D size N) + `.cdl/.ccc`(slope/offset/power+sat) 파서 → NxNxN RGB float 그리드로 정규화. CDL은 그리드로 베이크(공용 경로).
- `src/gl_texture.{h,cpp}`(74 뒤): `gl_upload_lut3d(const float* rgb,int N)` = `glTexImage3D(GL_TEXTURE_3D,0,GL_RGB32F,...)` + LINEAR/CLAMP_TO_EDGE(S/T/R).
- `src/shader_sources.h IMAGE_FRAG_SRC`(50 뒤·채널isolate 앞): `uniform sampler3D u_lut; uniform int u_lut_enabled;` → `if(u_lut_enabled==1) out_color.rgb=texture(u_lut,out_color.rgb).rgb;`.
- `src/ui/image_panel.cpp render_single`(uniforms 1880 뒤): LUT 텍스처 unit1 바인딩 + uniform. `cpu_render_image`(176 뒤): 삼선형 CPU 샘플.
- `src/app.h`: `AppState.lut{grid,N,enabled,texture_id}`; `CliOptions.lut_path/cdl_path`. `parse_cli` `--lut/--cdl`; MainWindow init에서 1회 업로드; handle_keyboard 토글 키; .av.ini 영속(선택).
- **스코프**: v1은 **표시(양 패널) 적용**만. diff/SSIM/FLIP 이전 적용(pre-diff LUT)은 후속(별도 노트).
- 검증: `--lut identity.cube`(항등)→화면 불변; 감마/색변환 .cube→양 패널 동일 변화; 토글; 소프트웨어 경로.

---

## 공통 마무리
- 빌드: `/opt/homebrew/bin/cmake --build build`(Mac). phase별 빌드+검증+원자 커밋. 신규 파일(lut.cpp)→CMakeLists AV_SOURCES.
- 3플랫폼(Mac install / Linux alexws 오프라인+심링크 / Windows Demura WSL + `C:\Windows` UAC[대화형 소켓]) 빌드·설치 — 전 기능 완료 후 일괄. 검증은 `--metrics`/`--validate`/`--diff-out` 헤드리스로 각 머신 실제 동작 확인.
- 검증 한계: 부호차맵·3D LUT·NaN 오버레이의 최종 육안은 디스플레이 필요. #1·#3지표·#4·#5(>1)은 헤드리스 완전검증.
- 교훈/문서: 함정 발생 시 tasks/lessons.md 갱신. 승인 후 이 계획을 proj_home/plans/로 복사, tasks/todo.md 체크리스트 생성.
