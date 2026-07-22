# av — 4개 신규 기능 구현 계획 (콜러메트리/균일도/Luma-Chroma/batch)

## Context

`av`는 DDI(디스플레이 드라이버 IC) 보상 IP 출력(보상 B vs 기준 A)을 검증하는 C++20/SDL3/ImGui/OpenGL 이미지 비교기다. 이전 세션에서 9개 기능(헤드리스 `--metrics`, CI 게이트, Signed diff, `--diff-out`, validator, 3D LUT 등)을 넣었고, 백로그(핸드오프 §7)에서 **레버리지 높은 4개**를 순서대로 구현한다. ①이 만드는 **컬러메트리 코어**를 ②가 재사용하는 의존이 있어 순서가 자연스럽다.

핵심 발견(탐색 결과):
- 중앙 `color.{h,cpp}` **부재**. sRGB EOTF·RGB↔XYZ·Lab는 `flip_engine.cpp` 익명 네임스페이스에만 존재(공유 불가). **xy·u'v'·CCT·Duv·YCbCr·ΔE는 코드베이스에 전혀 없음** → 신규 작성.
- `flip_engine.cpp`는 NVIDIA ꟻLIP 레퍼런스와 5자리 일치 검증본이며 자체 D65(`{0.95043,1,1.0889}`)를 씀 → **건드리지 않는다**. 새 모듈은 표준 CIE 2° D65(`0.95047,1,1.08883`)를 authoritative로 별도 사용(회귀 안전 + 원칙적으로 맞음).
- 재사용 자산: `RoiState`(app.h:209) + `compute_roi_stats`(chart_export.cpp:882, 채널별 mean 제공), hover balloon(main_window.cpp:1696~, V키), `compute_diff_stats` 2분기(chart_export.cpp:176/213)의 기존 luma 1-pass 루프, `run_metrics_headless`의 Row/gate/aggregate/emit 파이프라인(metrics_cli.cpp), 헤드리스 디스패치(main.cpp:194-213), `decode_image_cpu`(HDR=f32/LDR=u8, 항상 RGBA).

각 기능 = 독립 phase, 각자 빌드·헤드리스 검증·**로컬 커밋**(푸시 없음, Mac 빌드만 — 사용자 확정).

---

## Phase 0 — 공유 컬러 코어 `src/color.{h,cpp}` (신규, Phase 1에 포함)

새 모듈. 전부 `double`, D65 = CIE 2° `{0.95047, 1.0, 1.08883}`.
- `struct XYZ {double X,Y,Z;}`
- `double srgb_to_linear(double)` / `linear_to_srgb(double)` — IEC sRGB(브레이크포인트 0.04045/0.0031308).
- `XYZ srgb_to_xyz(double r,g,b)` — 입력 sRGB 디스플레이값[0,1] → 선형화 → sRGB/Rec.709 D65 행렬(flip_engine 값과 동일 계수) → XYZ.
- `XYZ lin_rgb_to_xyz(double r,g,b)` — HDR(선형) 직접 경로.
- `void xyz_to_xy(XYZ,&x,&y)`; `void xyz_to_upvp(XYZ,&up,&vp)` (u'=4X/(X+15Y+3Z), v'=9Y/(…)).
- `double cct_mccamy(double x,double y)` — n=(x−0.3320)/(0.1858−y); CCT=−449n³+3525n²−6823.3n+5520.33.
- `double duv_ohno(double up,double vp)` — 1960 UCS(u=u', v=⅔v')에서 Planckian locus 근사(Ohno 다항식), 부호 포함.
- `void xyz_to_lab(XYZ,&L,&a,&b)` + `double delta_e76(Lab,Lab)` — 프로브 Δ용.
- `CMakeLists.txt` `AV_SOURCES`(L124 `lut.cpp` 뒤)에 `src/color.cpp` 추가.

검증(색 코어 정확도): 순색 합성 패치로 알려진 값 대조 — 순백 sRGB(255,255,255)→ x≈0.3127,y≈0.3290,CCT≈6504K, Duv≈0; 순적 sRGB→ x≈0.640,y≈0.330. `--probe`로 기계 검증(Phase 1).

---

## Phase 1 — 컬러메트리 프로브

**헤드리스 `--probe X,Y A [B]`** (수학 증명 경로):
- `CliOptions`(app.h:249~)에 `std::string probe;` 추가. `parse_cli`(app.cpp:95~)에 `--probe`(값=`"X,Y"`) 파싱. main.cpp 디스패치 블록(:203 앞)에 `if(!cli.probe.empty()) return run_probe_headless(cli);`.
- `run_probe_headless`(metrics_cli.cpp, 또는 신규 `probe`용이지만 metrics_cli에 두어 `decode_image_cpu` 재사용): A(+B) 디코드, (X,Y) 픽셀 샘플. LDR=u8/255→sRGB경로, HDR=f32 선형→lin_rgb_to_xyz. CSV 출력: `which,X,Y,Z,x,y,uprime,vprime,CCT,Duv,Lstar`; B 있으면 A/B 행 + `delta` 행(ΔE76, ΔCCT, Δu'v'). exit: 3 인자/좌표오류, 4 디코드실패.

**GUI 프로브** (대화형):
- `AppState`에 `bool show_colorimetry=false`. 토글키 `Shift+V`(app.cpp 스캔코드 스위치; V=show_pixel_info 옆). hotkey-help(main_window.cpp:896~) 행 추가.
- hover balloon의 non-diff readout(main_window.cpp:1799-1843)에서, `show_pixel_info && show_colorimetry`이면 샘플 RGB를 color.h로 변환해 A/B의 x,y·u'v'·CCT + Δu'v'/ΔCCT 라인을 balloon에 append. 좌표/버퍼우선순위 로직은 기존 그대로 재사용.
- ROI stats window(chart_windows.cpp:1152 `render_roi_stats_window`)에 콜러메트리 행 추가: `compute_roi_stats`가 이미 주는 채널별 **mean R/G/B**를 color.h에 먹여 ROI-평균 x,y·u'v'·CCT를 A/B/Δ로 표시(추가 순회 불필요).

검증: `--probe`로 순색 패치 대조(위 기준값). GUI는 육안(사용자 화면).

---

## Phase 2 — 비균일도 메트릭 팩

**헤드리스 `--uniformity A [B]`** (평판 균일도, 전부 CI 검증):
- `CliOptions`에 `bool uniformity=false;` + 게이트 `float fail_uniformity=-1, fail_semu=-1;`. `parse_cli`에 `--uniformity`(bool), `--fail-uniformity <pct>`, `--fail-semu <val>` 추가. main.cpp 디스패치에 `if(cli.uniformity) return run_uniformity_headless(cli, pair_dir_b);`.
- `run_uniformity_headless`(metrics_cli.cpp): 이미지당 luma 필드(Rec.709 Y, color.h) 계산 후:
  - **ICDM N-point 휘도균일도%**: 9(3×3)/13/25(5×5) 격자점 luma → `uniformity_N = 100·Lmin/Lmax`.
  - **CV 비균일도%**: 전 픽셀 luma의 `100·std/mean`.
  - **색균일도 Δu'v'_max**: 격자점 u'v' vs 필드평균 u'v'의 최대거리(color.h).
  - **SEMU 프록시**: luma에 대형 박스 저역통과 → residual, 최악영역 Weber대비 `Cmura=|ΔL|/Lbg`, `SEMU=100·Cmura/(1.97/S^0.33+0.72)` — **면적 S는 픽셀기반 가정(균일 픽셀피치)이며 stderr/help에 가정 명시**(사용자 확정: 프록시 허용).
  - CSV: `file,w,h,Lmean,uni9,uni13,uni25,nonuni_cv,duv_max,semu`. 두 이미지(A,B) 또는 `--pair`면 A행·B행 + **Δ행**(uni%↑, cv↓, semu↓ = 보상 개선). 게이트 위반 시 exit 10.
- `--pair` 열거는 기존 `scan_image_directory` + 동일 basename 페어링 재사용.

검증: 합성 필드로 기계 검증 — 완전 평탄 필드→uni≈100%/cv≈0/semu≈0; 선형 램프→알려진 uni%; 국소 무라 삽입→semu 상승. `--fail-uniformity`로 exit 10 확인.

---

## Phase 3 — Luma/Chroma 분리 PSNR

**메트릭(psnr_cb/psnr_cr, 헤드리스 증명)**:
- `DiffExtraStats`(chart_export.h:32)에 `mse_cb,psnr_cb,mse_cr,psnr_cr` 추가.
- `compute_diff_stats`(chart_export.cpp) HDR분기(:199 루프)·u8분기(:237 루프)의 **기존 luma 1-pass 루프 안**에서 Rec.709 Cb/Cr 동시 누적: `Cb=(B−Y)/1.8556, Cr=(R−Y)/1.5748`(f32), u8은 동식(0-255 스케일). MSE→PSNR(peak: f32=1.0, u8=255, luma와 동일 관례). **새 루프 없음** → 성능·회귀 영향 최소.
- `Metrics`(metrics_cli.cpp:71)에 `psnr_cb,psnr_cr`. CSV 14→16열: **끝에 append** `...,msigned,psnr_cb,psnr_cr`(기존 14열 위치 불변 → 기존 파서 안전). `CSV_HEADER`, `print_row`(fmtv 2개↑), `placeholder_row`(끝 빈칸 10→12), `emit_json`(키 2개↑) 갱신.

**표시(Luma 그레이뷰)**:
- `ChannelMode`(app.h:37)에 `Luma=4` 추가. 이미지 표시 셰이더(shader_sources.h:55-57)에 `else if(u_channel==4){ float y=dot(rgb,vec3(.2126,.7152,.0722)); out=vec4(vec3(y),a);}`. CPU 미러: 표시 헬퍼(image_panel.cpp:99-103) + 픽셀 readout 포맷. **diff 셰이더 분기는 ==1/2/3만 검사 → u_channel==4는 자동 fallthrough(전체 diff 표시)** = 무영향·회귀안전. 토글 `Shift+L`(app.cpp), hotkey-help 갱신. `.av.ini` int 캐스트라 Luma=4 자동 영속.

검증: 컬러쌍(채널별 오차 다른)으로 `--metrics` → psnr_y≠psnr_cb≠psnr_cr 발산 확인. 순루마 차이만 있는 쌍 → chroma PSNR=inf. Luma뷰 육안.

---

## Phase 4 — `--batch` 매니페스트 드라이버

**임의 A,B 쌍 헤드리스**(`--pair` 동일basename 제약 없음):
- `run_metrics_headless` 꼬리(집계+emit+exit, metrics_cli.cpp:309-352)를 헬퍼 `static int finish_rows(cli, rows&, gate)`로 추출(집계+emit, `(gate&&nfail>0)?10:0` 반환). 단일쌍 mismatch→exit5 특례는 `run_metrics_headless`에 잔류. → **run_metrics/run_batch가 emit 로직 공유(DRY)**.
- `CliOptions`에 `std::string batch_path;`. `parse_cli`에 `--batch <file|->`. main.cpp 디스패치 맨앞에 `if(!cli.batch_path.empty()) return run_batch_headless(cli);`.
- `run_batch_headless`(metrics_cli.cpp): 매니페스트(파일 또는 `-`=stdin) 파싱 — 라인당 `A<TAB>B[<TAB>label]`, `#`주석·빈줄 스킵. 쌍마다 Row(name=label||basename(A)), `decode_image_cpu`×2(실패=status "missing"/"decode_error", `--pair`와 동일 관례), `compute_pair_metrics`, mismatch→status, else `gate_verdict`. `finish_rows` 호출. `--format`/`--fail-*` 기존 파싱 그대로 재사용. exit: 3 매니페스트 없음/빈/열기실패, 게이트 10.

검증: TAB 구분 매니페스트(공백 포함 경로·label 포함)로 CSV/json/junit 각각 확인, stdin(`-`) 경로, `--fail-psnr`로 exit 10, 존재하지 않는 B → row status "missing".

---

## 파일 변경 요약

| 파일 | 변경 |
|---|---|
| `src/color.{h,cpp}` (신규) | 컬러 코어(P0) |
| `CMakeLists.txt` | `AV_SOURCES`에 `color.cpp` |
| `src/app.h` | `CliOptions`: probe/uniformity/fail_uniformity/fail_semu/batch_path; `AppState.show_colorimetry`; `ChannelMode::Luma=4` |
| `src/app.cpp` | `parse_cli` 신 플래그, help 텍스트, `Shift+V`/`Shift+L` 키 |
| `src/main.cpp` | probe/uniformity/batch 헤드리스 디스패치(:203 블록) |
| `src/metrics_cli.{h,cpp}` | `run_probe/uniformity/batch_headless`, `finish_rows` 추출, `Metrics` 2열, CSV/json/junit |
| `src/chart_export.{h,cpp}` | `DiffExtraStats` Cb/Cr, `compute_diff_stats` 루프 확장 |
| `src/shader_sources.h` | 표시 셰이더 Luma 분기 |
| `src/ui/image_panel.cpp` | CPU Luma 미러 + readout |
| `src/ui/main_window.cpp` | balloon 콜러메트리 append, hotkey-help |
| `src/ui/chart_windows.cpp` | ROI stats window 콜러메트리 행 |

## 빌드/검증 (Mac, 기능별)
```bash
/opt/homebrew/bin/cmake --build build           # 각 phase 후
./bin/av --version
# P1: ./bin/av --probe 0,0 white.png             # x≈0.3127 y≈0.3290 CCT≈6504
# P2: ./bin/av --uniformity flat.png             # uni≈100 cv≈0 semu≈0
#     ./bin/av --uniformity A B --fail-uniformity 95; echo $?
# P3: ./bin/av --metrics colA.png colB.png        # 16열, psnr_cb≠psnr_cr
# P4: printf 'A\tB\n' | ./bin/av --batch - --metrics
```
각 phase 빌드 clean + 헤드리스 검증 통과 → 개별 커밋(메시지 `feat: …`, Co-Authored-By 포함). **푸시·3플랫폼 배포는 하지 않음**(사용자 승인 후 별도). 회귀 방지: 기존 `--metrics` 14열의 앞 14 위치·기존 diff/FLIP/SSIM 경로 불변 확인.
