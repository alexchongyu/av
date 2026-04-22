# Image Copy Function — 계획서

**작성일**: 2026-04-22
**기능**: `Cmd+C` (macOS) / `Ctrl+C` (Linux/Windows) 누른 후 `1` / `2` / `3` 누르면 각각 Image A / Image B / Diff 이미지를 PNG로 클립보드 복사

---

## 1. 목표 및 요구 사항

### UX 시나리오
1. 사용자가 `Cmd+C` (macOS) 또는 `Ctrl+C` (Linux/Windows) 를 누른다.
2. 화면 중앙 하단에 힌트 오버레이가 뜬다: `Copy: 1 = A, 2 = B, 3 = Diff (Esc to cancel)`
3. 사용자가:
   - `1` → Image A (swap 반영) 를 PNG로 클립보드에 복사, 오버레이 해제
   - `2` → Image B (swap 반영) 를 PNG로 클립보드에 복사, 오버레이 해제
   - `3` → 현재 Diff 모드 기준으로 계산한 Diff 이미지를 PNG로 클립보드에 복사, 오버레이 해제
   - `Esc` 또는 그 외 키 → 모드 취소 (복사 없음)
4. 5초 타임아웃 후 자동 모드 해제.
5. 복사 성공 시 상태바 또는 짧은 토스트 메시지로 피드백.

### 기술 요구 사항
- **Linux 컴파일 호환성 유지** — SDL3 클립보드 API + stb_image_write로 이미 플랫폼 독립적이므로 추가 작업 불필요.
- **Help 윈도우 엔트리 등록** (`main_window.cpp`의 `entries[]` 배열).
- **Typst 문서 업데이트** — `av_cheat_sheet.typ` 에 단축키 엔트리 추가, `av-app-implementation.typ` 에 간단한 설명 추가.
- **빠른 병렬 처리** — 탐색은 서브에이전트로 병렬 수행 완료.

---

## 2. 코드베이스 조사 결과 (완료)

### 재사용 가능한 자산
- **`ImagePanel::copy_panel_to_clipboard(AppState&, int target_type)`** (`src/ui/image_panel.cpp:1079`) — `target_type`: 0=A, 1=B, 2=Diff. swap_images도 반영. PNG 인코딩 + SDL_SetClipboardData까지 처리. **그대로 재사용.**
- **`compute_diff_cpu(imgA, imgB, diff)`** (`src/image_save.cpp:153`) — Diff RGBA8 버퍼 계산.
- **SDL3 `SDL_SetClipboardData` + `stb_image_write`** — macOS/Linux/Windows 공통 동작.

### 키 이벤트 시스템
- `main.cpp:399-410` — SDL_EVENT_KEY_DOWN → `handle_keyboard(state, scancode, ctrl, shift, alt, gui)` 호출.
- `app.cpp:154` `handle_keyboard()` — scancode 기반 switch.
- **두 단계 상태 기반 모달 패턴 이미 존재** — ROI (`state.roi.active`), Overlay (`state.overlay.active`). 동일 패턴 적용.

### 현재 빈 슬롯
- `SDL_SCANCODE_C` 는 현재 `Shift+C` (Channel RGB) 만 처리 중 — `(ctrl || gui)` 조합은 **비어 있음**. 충돌 없이 추가 가능.
- 숫자 키 `1/2/3` 은 현재 app.cpp에서 전역적으로 처리하지 않음 (확인 필요).

### Help 등록 위치
- `src/ui/main_window.cpp:774-869` — `HotkeyEntry entries[]` 정적 배열. 카테고리 "File" 또는 신설 "Copy" 에 3개 엔트리 추가.

### Typst 문서
- `doc_typst/av_cheat_sheet.typ:155 이후` — `#key("...")` 매크로 기반 2열 테이블.
- `doc_typst/av-app-implementation.typ` — 상세 구현 가이드. 새 섹션 하나 추가.

---

## 3. 수정 파일 목록 및 변경 내용

### [A] `src/app.h` — 상태 구조체 추가
- `CopyModeState` 구조체 추가:
  ```cpp
  struct CopyModeState {
      bool active = false;
      double started_at = 0.0;  // ImGui::GetTime()
      double hint_until = 0.0;  // 복사 성공 후 토스트 표시 시간
      int last_copied = -1;     // 0/1/2, -1=없음 (토스트용)
      void reset() { active = false; started_at = 0.0; }
  };
  ```
- `AppState`에 `CopyModeState copy_mode;` 필드 추가.

### [B] `src/app.cpp` — 키 이벤트 처리
1. **Cmd/Ctrl + C 진입 (기존 `case SDL_SCANCODE_C:` 확장)**
   ```cpp
   case SDL_SCANCODE_C:
       if (shift) state.channel_mode = ChannelMode::RGB;
       else if ((ctrl || gui) && !alt) {
           // 복사 모드 진입
           state.copy_mode.active = true;
           state.copy_mode.started_at = ImGui::GetTime();
       }
       break;
   ```

2. **숫자 키 1/2/3 처리 — 복사 모드 진행 시에만 가로챔**
   - handle_keyboard 시작 부분에서 `state.copy_mode.active` 체크.
   - `SDL_SCANCODE_1 / _2 / _3` → 대응 이미지 복사 후 mode reset.
   - `SDL_SCANCODE_ESCAPE` 또는 그 외 키 → mode reset (복사 없음).
   - 복사 호출은 새 헬퍼 함수 `clipboard_copy_image(state, idx)` 로 위임 (C에서 호출 가능하도록 free function으로).

3. **타임아웃 체크** — handle_keyboard는 키 이벤트에서만 호출되므로 타임아웃은 main render 루프에서 체크 (아래 [D]).

### [C] `src/ui/image_panel.cpp` — `copy_panel_to_clipboard` 접근성 + 토스트
1. **기존 메서드를 free function으로 리팩터링하거나 얇은 래퍼 노출**
   - `ImagePanel::copy_panel_to_clipboard` 내부 로직 변경 없음. 다만 app.cpp에서 호출할 수 있도록:
     - 옵션 A: 헤더(`image_panel.h`)에 `void clipboard_copy_image(AppState&, int target_type);` free function 선언 추가, 구현은 `image_panel.cpp`에 두고 기존 메서드가 이를 호출하도록 바꿈.
     - **옵션 B (채택)**: 공용 clipboard 유틸을 새 파일 `src/clipboard_image.h` + `src/clipboard_image.cpp`로 분리. 더 깔끔하고 디펜던시 그래프도 단순해짐 (ui 레이어 의존성 제거).
   - 선택: **옵션 B**.
2. 새 파일 `src/clipboard_image.cpp`:
   - 기존 정적 헬퍼 (`stbi_mem_write_func`, `s_clipboard_png`, `clipboard_data_callback`, `clipboard_cleanup_callback`) 를 이 파일로 이동.
   - `void clipboard_copy_image(AppState& state, int target_type)` 구현.
   - `ImagePanel::copy_panel_to_clipboard` 는 `clipboard_copy_image(state, target_type)` 한 줄로 축소.

### [D] `src/ui/main_window.cpp` — 렌더 루프에 힌트 오버레이 + 타임아웃
- `MainWindow::render()` 말미에 복사 모드 오버레이 렌더링 추가.
  - `copy_mode.active`이면 화면 하단 중앙에 반투명 박스 + 텍스트 `Copy: 1 = A, 2 = B, 3 = Diff (Esc to cancel)`.
  - `ImGui::GetTime() - started_at > 5.0` 이면 `copy_mode.reset()`.
- 복사 성공 토스트 (선택 사항): `copy_mode.last_copied >= 0 && GetTime() < hint_until` 이면 `"Copied: Image A"` 같은 메시지 1.5초 표시 후 사라짐.

### [E] `src/ui/main_window.cpp` — Help 엔트리 등록
- `entries[]` 배열에 추가:
  ```cpp
  { "Copy", "Ctrl/Cmd+C, then 1", "Copy Image A to clipboard as PNG" },
  { "Copy", "Ctrl/Cmd+C, then 2", "Copy Image B to clipboard as PNG" },
  { "Copy", "Ctrl/Cmd+C, then 3", "Copy Diff image to clipboard as PNG" },
  ```

### [F] `CMakeLists.txt` — 새 소스 파일 추가
- `src/clipboard_image.cpp` 를 `add_executable(av ...)` 소스 리스트에 추가.

### [G] `doc_typst/av_cheat_sheet.typ` — 단축키 테이블
- 새 섹션 "복사" 또는 기존 "파일" 섹션에 추가:
  ```typst
  [#key("Ctrl/Cmd+C") → #key("1")], [Image A를 PNG로 클립보드 복사],
  [#key("Ctrl/Cmd+C") → #key("2")], [Image B를 PNG로 클립보드 복사],
  [#key("Ctrl/Cmd+C") → #key("3")], [Diff 이미지를 PNG로 클립보드 복사],
  ```
- `typst compile` 로 PDF 재생성.

### [H] `doc_typst/av-app-implementation.typ` — 구현 설명
- "클립보드 복사" 소절 하나 추가 (짧게):
  - 2단계 키 시퀀스 패턴, `clipboard_copy_image()` 함수 위치, 플랫폼 추상화 설명.
- `typst compile` 로 PDF 재생성.

---

## 4. Linux 컴파일 호환성

**결론: 이미 지원됨, 추가 변경 불필요.**

- `CMakeLists.txt:35,154-168,180,217` — UNIX 분기 이미 있음, AppImage까지 자동 빌드.
- SDL3 `SDL_SetClipboardData` — 리눅스에서 X11/Wayland 자동 대응.
- `stb_image_write` — 헤더만 필요, 모든 플랫폼 공통.
- 새 파일 `clipboard_image.cpp` 는 플랫폼 의존 코드 없음.

**검증 방법**: macOS에서 `ninja -C build` 성공 후 wsl/Linux 환경 있으면 동일 빌드. (실행 중인 시스템은 macOS이므로 macOS 빌드만 이 계획 내에서 수행, Linux 빌드는 코드상 안전성 확보로 충분.)

---

## 5. 엣지 케이스

- **이미지 미로드**: `copy_panel_to_clipboard` 내부에서 이미 체크 (`if (!img.loaded) return;`). 무반응. 토스트로 `"No image loaded"` 표시는 선택 사항.
- **Diff 모드 None 상태에서 3 누르기**: `compute_diff_cpu` 는 기본 `PixelAbsolute`로 계산하므로 정상 동작. (현재 `Copy to Clipboard` 컨텍스트 메뉴도 동일 동작.)
- **ImGui 텍스트 입력 포커스 중 Cmd+C**: `main.cpp:407`의 `if (!io.WantCaptureKeyboard || ctrl || gui)` 때문에 현재 조합은 항상 통과. 이게 문제 — 텍스트 입력 중 복사 모드에 빠질 수 있음.
  - **해결**: `app.cpp`의 `SDL_SCANCODE_C` 핸들러에서 `ImGui::GetIO().WantCaptureKeyboard` 이면 모드 진입 skip.
- **복사 모드 중 시스템 복사 동작 차단**: 복사 모드는 짧은 타임아웃이므로 무관. ImGui 텍스트 박스 포커스 중에는 모드 진입하지 않으므로 텍스트 복사 정상.
- **`1/2/3`은 이미 다른 기능과 충돌하는지**: `app.cpp`에 숫자 키 핸들러 없음 확인 완료. `ImGui`가 숫자 키를 InputText에서 쓸 수 있으나, 복사 모드는 `!WantCaptureKeyboard` 일 때만 활성화됨.

---

## 6. 단계별 실행 순서 (병렬 가능한 부분 표기)

### Phase A — 코드 (직렬, 빌드로 검증)
1. `src/app.h` — `CopyModeState` 추가, `AppState::copy_mode`.
2. `src/clipboard_image.h` + `src/clipboard_image.cpp` 신설 — 기존 image_panel.cpp static 헬퍼 이동 + `clipboard_copy_image(AppState&, int)` 구현.
3. `src/ui/image_panel.cpp` — `copy_panel_to_clipboard` 를 새 함수 호출로 축소, static 제거.
4. `src/ui/image_panel.h` 등에 include 조정.
5. `src/app.cpp` `handle_keyboard()` — Cmd/Ctrl+C 진입, 모드 중 1/2/3/Esc 처리, WantCaptureKeyboard 가드.
6. `src/ui/main_window.cpp` — 복사 모드 오버레이 렌더링 + 타임아웃, Help `entries[]` 3엔트리 추가.
7. `CMakeLists.txt` — 새 소스 추가.

### Phase B — 빌드 및 검증 (직렬)
8. `ninja -C build` — 빌드 성공 확인.
9. `ninja -C build install` — 설치.
10. 수동 검증:
    - 이미지 두 장 로드 → `Cmd+C` → 힌트 표시됨.
    - `1` → Image A가 클립보드에 들어감 (외부 앱에서 paste 확인).
    - `2` → Image B 복사 확인.
    - `3` → Diff 이미지 복사 확인.
    - `Esc` → 모드 취소.
    - 5초 후 자동 해제 확인.
    - 텍스트 입력창 포커스 중 Cmd+C → 복사 모드 진입 안 됨.

### Phase C — 문서 (병렬 가능)
11. `doc_typst/av_cheat_sheet.typ` 수정 + `typst compile`.
12. `doc_typst/av-app-implementation.typ` 수정 + `typst compile`.
    *(두 문서는 독립이므로 병렬 편집 후 병렬 컴파일 가능)*

### Phase D — 커밋
13. 단일 커밋 (또는 기능 + 문서 2커밋). 릴리스 태그는 사용자 요청 시 별도.

---

## 7. 리스크 & 미리 판단한 결정

| 리스크 | 판단 |
|---|---|
| 기존 `copy_panel_to_clipboard` 를 free function으로 분리 시 영향 | ImagePanel 메서드는 얇은 래퍼로 남기므로 우클릭 컨텍스트 메뉴 영향 없음 |
| 2단계 키 모드 중 ImGui 포커스가 바뀔 경우 | 타임아웃으로 자동 해제, 우려 없음 |
| 토스트 메시지 구현 복잡도 | 1차 구현에서는 오버레이 힌트만, 복사 성공 시 같은 오버레이를 잠깐 색상만 바꿔 표시 (간단화) |
| Linux에서 image/png mime 클립보드 동작 | SDL3 이미 처리, av의 기존 우클릭 복사도 동일 경로 사용 중이므로 안전 |

---

## 8. 승인 요청

**이 계획대로 진행해도 될까요?** 
승인하시면 순서대로 구현 및 빌드 검증 후 보고 드립니다. 문서 PDF 재생성까지 포함합니다.

변경이 필요한 부분 (예: UX 세부 — 토스트 여부, 키 시퀀스 타임아웃, Help 카테고리명 등) 이 있으면 알려주세요.
