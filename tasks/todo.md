# av 프로젝트 작업 목록

## [완료] av-spec.typ 사양서 작성 및 PDF 생성

### 작업 내역
- [x] `doc_typst/av-spec.typ` 작성
- [x] `typst compile av-spec.typ` → `av-spec.pdf` (488K) 생성 성공

### 결과물
- **문서**: `doc_typst/av-spec.typ`
- **PDF**: `doc_typst/av-spec.pdf`

### 검토
- typst 0.14.2 컴파일 오류 없음
- CMU Serif (영어) + Noto Sans CJK KR (한글) 폰트 적용
- cetz 0.4.2 다이어그램 4개 포함:
  - 표지 로고 (이미지 뷰어 아이콘)
  - 모듈 의존성 다이어그램 (3계층 구조)
  - 데이터 흐름 다이어그램 (6단계 파이프라인)
  - 이미지 비교 모드 개요 (4가지 모드)
  - 구현 로드맵 (5단계 + 타임라인)
- 챕터별 페이지 분리 적용
- 표지, 차례, 7개 챕터 구성

## [완료] Phase 1~3 구현 (2026-02-27)

### 생성된 파일 (19개)
- [x] `CMakeLists.txt` — SDL3 + glad2 + ImGui(docking) + stb + lcms2
- [x] `src/app.h` — AppState, ViewportState, DiffState, CliOptions
- [x] `src/shader_sources.h` — GLSL #version 150 셰이더 소스
- [x] `src/stb_impl.cpp` — STB 구현 파일
- [x] `src/viewport.h/.cpp` — pan/zoom/fit 변환
- [x] `src/image_loader.h/.cpp` — stb_image 로딩, LRU 8개 캐시
- [x] `src/app.cpp` — CLI 파싱, 키보드 핸들링
- [x] `src/gl_texture.h/.cpp` — 텍스처, ShaderProgram, ScreenQuad, FBO
- [x] `src/diff_engine.h/.cpp` — GPU diff 렌더러, CPU SSIM (jthread)
- [x] `src/ui/main_window.h/.cpp` — ImGui dockspace + 메뉴바
- [x] `src/ui/image_panel.h/.cpp` — FBO 기반 이미지 패널
- [x] `src/ui/statusbar.h/.cpp` — 상태 표시줄
- [x] `src/main.cpp` — SDL3/OpenGL/ImGui 초기화 + 메인 루프

### 빌드 결과
```
bin/av: Mach-O universal binary (arm64 + x86_64)  8.0 MB
cmake --build build  → 오류 없음 (경고만)
./bin/av --version   → "av 0.1.0"
./bin/av --help      → 옵션 목록 표시
```

### 해결한 이슈
1. `project(av C CXX)` — glad2 C 소스 생성을 위해 C 언어 선언 필요
2. ImGui GL 타입: `imgui_lib → glad_gl` 링크 (IMGUI_IMPL_OPENGL_LOADER_CUSTOM 제거)
3. `static_cast<ImTextureID>` — ImU64 타입 캐스트
4. `pkg_check_modules(IMPORTED_TARGET lcms2)` — 링크 경로 수정

### 빌드 명령
```bash
/opt/homebrew/bin/cmake -B build -DCMAKE_BUILD_TYPE=Release
/opt/homebrew/bin/cmake --build build
./bin/av image.png
./bin/av a.png b.png
./bin/av --diff-mode=abs a.png b.png
```

## [완료] Phase 4 기능 추가 (2026-02-27)

### 변경 내역
- [x] `av a.bmp b.bmp` — 두 이미지 side-by-side 표시 (dockspace 제거, child window 레이아웃)
- [x] 기본 UI 없음: `show_ui = false` (AppState)
- [x] `U` 키 — UI (메뉴바 + 상태바) 토글
- [x] `-d` / `--diff` CLI 옵션 — PixelAbsolute diff 모드 즉시 활성화

### 아키텍처 변경
- `main_window.cpp`: ImGui dockspace → 단순 child window 레이아웃으로 교체
  - `show_ui == false`: 메뉴바·상태바 없이 이미지만 표시
  - `show_ui == true`: 메뉴바 + 상태바 표시
  - side-by-side: `BeginChild("##PanelLeft", half_w) + SameLine + BeginChild("##PanelRight")`
- `main.cpp`: `ImGuiConfigFlags_ViewportsEnable` 및 multi-viewport 코드 제거

## [완료] Advanced Pixel Lens — 앱 이름/아이콘 + Pathfinder + Border 수정 (2026-02-27)

### 변경 내역
- [x] `src/main.cpp` — APP_TITLE → "Advanced Pixel Lens"
- [x] `src/main.cpp` — `create_app_icon()` 128×128 RGBA 프로시저럴 아이콘 (배경+라운드코너+3×3컬러그리드+돋보기), `SDL_SetWindowIcon` 호출
- [x] `src/app.h` — `show_pixel_info` → `show_pathfinder = true` (기본 ON)
- [x] `src/app.cpp` — P키 핸들러: `show_pathfinder` 토글
- [x] `src/ui/main_window.cpp` — 메뉴 아이템: "Show Pathfinder"
- [x] `src/ui/image_panel.h` — `render_pathfinder()` 선언 추가
- [x] `src/ui/image_panel.cpp` — `draw_image_border()`: 4개 edge를 개별 `AddLine`으로 분리 (줌 시 클리핑 수정)
- [x] `src/ui/image_panel.cpp` — `render_pathfinder()` 구현 (panel_idx==0, fit 모드 제외)
- [x] `src/ui/image_panel.cpp` — `render_single()` 끝에서 `render_pathfinder()` 호출

### 검증
- cmake --build → 오류 없음 (경고만)
- 앱 실행 확인 완료

### 수정 이슈
- SDL3에서 `SDL_MapRGBA` 인자: `SDL_PixelFormat` enum → `SDL_GetPixelFormatDetails()`로 `const SDL_PixelFormatDetails*` 획득 필요
- `ImTextureID`(uint64_t)와 `uintptr_t`(uint32_t on x86?) 캐스트 — `static_cast<ImTextureID>` 사용

## [완료] Phase 5: 추가 분석 기능 구현 (2026-03-05)

### 구현 내용

#### 1. ROI (Region of Interest) 선택 + 영역 통계
- [x] `src/app.h`: `RoiState` 구조체 추가 (active, has_roi, x/y/w/h, dragging 등)
- [x] `src/chart_export.h/cpp`: `compute_roi_stats()`, `compute_roi_diff_stats()` 구현
- [x] `src/ui/image_panel.cpp`: ROI 드래그 선택 (`handle_roi_drag`), ROI 오버레이 표시 (`render_roi_overlay`)
- [x] `src/ui/chart_windows.h/cpp`: `render_roi_stats_window()` 구현 (A/B/Diff 통계 테이블)
- [x] `src/app.cpp`: `Ctrl+E` → ROI 모드 토글, ESC → ROI 모드 해제
- [x] `src/ui/main_window.cpp`: View 메뉴에 ROI Stats 항목, 창 렌더링 호출
- [x] `src/ui/statusbar.cpp`: ROI 모드 표시, ROI 좌표/크기 표시

#### 2. 이미지 시퀀스 탐색 (N/Shift+N)
- [x] `src/app.h`: `SequenceState` 구조체 추가 (files 벡터, current_index)
- [x] `src/app.h`: `AppState`에 `sequences[2]` 추가
- [x] `src/image_loader.h/cpp`: `scan_image_directory()` 구현 (natural sort)
- [x] `src/ui/main_window.cpp`: 이미지 로드 시 자동 시퀀스 스캔
- [x] `src/app.cpp`: `N` → 다음 프레임, `Shift+N` → 이전 프레임
- [x] `src/ui/statusbar.cpp`: `A [3/24]` 형식으로 현재/전체 프레임 수 표시

#### 3. Overlay/Blend 비교 모드 (O 키)
- [x] `src/app.h`: `OverlayState` 구조체 추가 (active, alpha, mode: Blend/Curtain)
- [x] `src/shader_sources.h`: `BLEND_FRAG_SRC` 셰이더 추가 (blend + curtain)
- [x] `src/ui/image_panel.h/cpp`: `render_overlay()` 구현 (blend_shader_)
- [x] `src/app.cpp`: `O` → Overlay 모드 토글
- [x] `src/ui/main_window.cpp`: Overlay 활성 시 1-panel 레이아웃 (blend 패널)
- [x] `src/ui/main_window.cpp`: View 메뉴에 Overlay 설정 (슬라이더, Blend/Curtain 선택)
- [x] `src/ui/statusbar.cpp`: Overlay 모드 표시 (모드 + alpha%)

#### 4. Scatter Plot (Ctrl+T)
- [x] `src/chart_export.h/cpp`: `ScatterPlotData` 구조체 + `extract_scatter_plot()` 구현
- [x] `src/ui/chart_windows.h/cpp`: `render_scatter_plot_window()` 구현
- [x] `src/app.h`: `AppState`에 `show_scatter_plot` 추가
- [x] `src/app.cpp`: `Ctrl+T` → Scatter Plot 토글
- [x] `src/ui/main_window.cpp`: View 메뉴에 Scatter Plot 항목, 창 렌더링 호출

### 단축키 정리
| 기능 | 단축키 |
|------|--------|
| ROI 모드 토글 | Ctrl+E |
| Overlay 토글 | O |
| Scatter Plot 토글 | Ctrl+T |
| 다음 시퀀스 프레임 | N |
| 이전 시퀀스 프레임 | Shift+N |

### 빌드 결과
- cmake --build → 오류 없음 (링커 경고만)
- `./bin/av --version` → av 0.1.0 정상 출력

## [계획] 코드 개선 — 버그→성능→리팩터링 (2026-06-24)

> 상세: `plans/av-fix-plan-20260624.md`. 원칙: No regression / 새 버그 금지.
> **승인 대기 중 — 코드 미수정.** 항목별 원자적 커밋 + 빌드/동작 검증 게이트.

### 0. 베이스라인 (착수 시)
- [x] 증분 빌드 성공 확인 (bin/av v0.22-6, known-good)

### Phase A — 치명 버그 (low risk) ✅ 완료 (origin 대비 push 대기)
- [x] A1. SSIM 데이터 레이스 → atomic release/acquire — `a56b7da`
- [x] A3. PPM ASCII CRLF → fopen "wb" 고정 — `abde37f`
- [x] A4. 소프트 AlphaBlend alpha 전달 ⚠️의도된 동작수정 — `d042d18`
- [x] A6. diff 모드 집합 단일 진실원(constexpr 테이블) — `5b3d79d`
- [x] A2. destructive load → 임시 entry 후 성공 시 커밋 — `db845a0`
- [x] A5. av-x11 size_t 오버플로우 + dim 클램프 (Linux 빌드) — `a5ee191`

### Phase B — 성능
- [ ] B3. ImageEntry content_version 스탬프 → dim-only 캐시 무효화
- [ ] B2. 소프트 diff 패스 융합(3→1, ⚠️캐싱 아님) — byte-identical
- [ ] B1. dead ImageCache 연결(copy-out, double-free 회피) + ±1 프리페치 (medium)

### Phase C — 리팩터링 (behavior-preserving)
- [ ] C2-A. diff 히스토그램 is_hdr 게이트 제거(화면==export) (chart_windows.cpp:300)
- [ ] C2-B. 화면 차트가 chart_export extractor 호출(중복 제거)
- [ ] C3. 저장 format switch 단일화 + 다이얼로그 boilerplate helper
- [ ] C1-1. viewport.h 좌표 헬퍼 추가
- [ ] C1-2. floor 표준화 ⚠️behavior-change(별도 커밋·승인)
- [ ] C1-3/4. inline 복사본 사이트별 교체(각 커밋 byte-compare)

### Phase D — 신기능 (A~C 후 재승인)
- [ ] 후보: 새 diff 모드, TIFF/EXR, JPEG 저장, 라인컷 위치 선택, 분석창 enum화
