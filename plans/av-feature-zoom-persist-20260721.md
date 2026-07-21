# 계획 — `--zoom` 옵션 구현·영속화 + ImGui ini 파일명 변경

## Context (왜 하는가)
사용자 요구:
1. **처음 화면을 영상의 "1배(1:1 실제크기)"로 띄우는 CLI 옵션**.
2. ImGui 레이아웃 ini `av_imgui.ini` → **`.av_imgui.ini`**(숨김)로 변경.
3. `--zoom` 값 체계 `fit(=0)|1|2 …` 로, **마지막 옵션 상태를 저장**. 단 **command line에서 명기한 옵션값을 저장**.

핵심 발견(근본 원인):
- `--zoom`은 `parse_cli`(`src/app.cpp:114`)에서 파싱되지만 **실제로 뷰에 적용되지 않는 죽은 옵션**이다.
  `apply_cli_options`(`app.cpp:169`)는 `state.cli`에 저장만 하고 뷰포트 zoom을 건드리지 않으며,
  `load_image_and_populate_sequence`(`image_loader.cpp:464`)가 **로드마다 `fit=true`로 리셋**한다.
  → 지금은 항상 창맞춤으로 열리고 `--zoom`은 무효.
- "1배(1:1)"의 앱 규약 = `viewport_set_zoom(v, 1.0f)` + `viewport_center(v)` (메뉴 "1:1 Pixel" `main_window.cpp:642`).
  `viewport_set_zoom`(`viewport.cpp:57`)은 `zoom=clamp(z,[0.125,256])` + **`fit=false`** 설정 → 매 프레임 fit에 덮이지 않음.

## 값 체계 & 영속화 의미(설계 결정)
- `--zoom` 인자: `fit` 또는 `0` → 창맞춤 / `1` → 1:1 실제크기 / `2` → 2배 / 임의 N → N배(범위 클램프).
- 저장 위치: 기존 앱 설정 ini `$HOME/.av.ini`(`load_app_ini`/`save_app_ini`)에 `zoom=` 키 추가.
- 우선순위/저장 규칙: **CLI `--zoom` 지정 시 저장값을 덮어쓰고 그 값이 저장됨. 미지정 시 저장된 값 사용.**
  기본값(둘 다 없을 때)=0(fit) → **기존 동작 보존(무회귀)**.
- 저장 대상은 **"옵션값"**(CLI 또는 직전 저장값)이며, 세션 중 키보드로 바꾼 라이브 줌은 저장하지 않음
  (사용자 확정: `av --zoom 2 img` → `zoom=2` 저장, 이후 키보드로 4배로 바꿔도 저장은 `zoom=2` 유지).

## 변경 파일

### `src/main.cpp`
- L341: `io.IniFilename = "av_imgui.ini";` → `".av_imgui.ini";`
- 시작 이미지 로드 직후(L423 뒤), 이벤트 루프 진입 전 — 초기 zoom 적용:
  ```cpp
  // 초기 zoom 적용: 0=fit(기본), >0=고정 배율. 로드가 매번 fit=true로 리셋하므로 최초 1회만.
  if (state.zoom_setting > 0.0f) {
      for (int i = 0; i < 2; ++i)
          if (state.images[i].loaded) {
              viewport_set_zoom(state.views[i], state.zoom_setting);  // fit=false + clamp
              viewport_center(state.views[i]);
          }
  }
  ```
  (`viewport.h` 이미 include됨 `main.cpp:3`.)

### `src/app.h`
- `CliOptions`: `bool zoom_set = false;` 추가(명시적 `--zoom` 지정 감지용). 기존 `float zoom` 유지.
- `AppState`: `float zoom_setting = 0.0f;  // 초기 zoom 옵션(영속): 0=fit, >0=배율` 추가.

### `src/app.cpp`
- `parse_cli` `--zoom`(L114): 분기에서 `opts.zoom_set = true;` 도 설정(`0`/`fit` → 0.0f = fit).
- `apply_cli_options`(L169): `if (opts.zoom_set) state.zoom_setting = opts.zoom;` 추가
  (load_app_ini 다음에 실행되므로 CLI가 저장값을 덮어씀 — 순서 확인 완료).
- `load_app_ini`: `else if (key == "zoom") state.zoom_setting = std::stof(val);` 추가.
- `save_app_ini`: `f << "zoom=" << state.zoom_setting << "\n";` 추가.
- `print_help`(L61): `--zoom` 설명을 `fit|0|1|2 …  (0/fit=창맞춤, 1=1:1 실제크기; 값 저장됨)` 로 갱신.

## 시작 순서(검증됨)
`load_app_ini`(저장 zoom 로드) → `apply_cli_options`(CLI --zoom 덮어쓰기) → 이미지 로드(fit=true)
→ **신규: zoom_setting 적용** → 이벤트 루프 → `save_app_ini`(zoom_setting 기록).

## 무회귀(No-Regression)
- 기본 `zoom_setting=0` → fit. `--zoom` 없고 저장값 없으면 기존과 동일(창맞춤).
- `zoom_set` 게이트로 `--zoom` 미지정 시 저장값을 절대 덮지 않음.
- ini는 키 추가만(하위호환): `zoom` 없는 기존 `.av.ini` → fit 유지.
- `av_imgui.ini`/`.av_imgui.ini` 둘 다 `.gitignore`의 `*.ini`에 포함 → git 영향 없음. 첫 실행 시 창 레이아웃 1회 초기화(예상됨).

## 검증
- 맥 로컬 빌드: `/opt/homebrew/bin/cmake --build build`.
- 케이스:
  - `av --zoom 1 IMG` → 1:1로 열림 / `~/.av.ini`에 `zoom=1` / 다시 `av IMG`(플래그 없이) → 여전히 1:1.
  - `av --zoom fit IMG` → 창맞춤 / `zoom=0` / 재실행 → 창맞춤.
  - `av --zoom 2 IMG` → 2배.
  - 실행 후 `.av_imgui.ini` 생성 확인(구 `av_imgui.ini` 아님).
- 무회귀 대조: 빈 `.av.ini`로 `av IMG` → 창맞춤(기존과 동일).
- (옵션) 승인 후 리눅스 배포는 `linux-compile-install.md` 런북으로.

## 마무리
- 승인 후 이 플랜을 CLAUDE.md 규칙대로 `proj_home/plans/`에도 날짜 파일명으로 복사.
- `tasks/todo.md` 체크리스트 작성, 완료 후 리뷰 섹션 기록.
