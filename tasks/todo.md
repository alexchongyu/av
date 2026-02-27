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

## 다음 작업 (Phase 5+)
- [ ] 파일 열기 다이얼로그 (SDL_ShowOpenFileDialog)
- [ ] Overlay (Swipe) 비교 모드
- [ ] 히스토그램 패널
- [ ] 설정 파일 저장/로딩
- [ ] 단위 테스트
