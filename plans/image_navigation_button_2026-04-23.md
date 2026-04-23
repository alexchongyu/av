# Image Navigation Button — Plan

**작성일:** 2026-04-23
**대상 기능:** 디렉토리 내 다른 이미지 파일로 "파일 선택 다이얼로그 없이" 네비게이션.
A/B 패널 각각 독립, 파일명 순 정렬, 끝까지 가면 처음으로 wrap-around, 로딩된
파일명은 짧게 토스트로 표시. 핫키 + 마우스 바인딩.

---

## 1. 배경: 이미 존재하는 인프라

새로 만들 필요가 **거의 없다**. 다음이 이미 구현되어 있음:

| 기능 | 위치 | 상태 |
|---|---|---|
| `SequenceState sequences[2]` (files + current_index) | `src/app.h:177,251` | ✅ |
| `scan_image_directory()` — 자연 정렬 파일 목록 | `src/image_loader.h:67` | ✅ |
| `ImageCache` LRU 8개 캐시 | `src/image_loader.h:25` | ✅ |
| `N` / `Shift+N` 핸들러 (wrap-around 포함) | `src/app.cpp:597-620` | ✅ |
| `open_state` pending 로드 파이프라인 | `src/ui/main_window.cpp:1207-1231` | ✅ |
| 슬라이드쇼 (`A` 키, `main.cpp:435-450`) | `src/main.cpp` | ✅ |
| Help 엔트리 "Next/Previous image in directory sequence" | `src/ui/main_window.cpp:874-876` | ✅ |

## 2. 실제로 비어 있는 구멍 (Gap)

### G1. **CLI 경로 · drag-drop 경로에서 `sequences[]` 가 채워지지 않는 버그**
- `main.cpp:369-377` (CLI) — `load_image()` 직접 호출, `scan_image_directory()` 생략
- `main.cpp:411-419` (drop_file) — 역시 `load_image()` 직접 호출
- 결과: `av a.png b.png` 로 실행 후 `N` 눌러도 반응 없음. `open_state` 다이얼로그
  경로로 연 뒤에만 동작.

### G2. **로딩된 파일명 토스트 오버레이 없음**
- 사용자가 요구: "로딩되는 파일 이름은 잠시라도 보여줘야" 함.
- 현재 타이틀 바(`main_window.cpp:1200` 근처)에는 반영되지만, **타이틀바는
  fullscreen·no-border 모드에서 보이지 않음**. 독립 토스트 필요.

### G3. **마우스 바인딩 없음**
- 현재 `TAB`으로 active_panel 전환 → `N`/`Shift+N` 이라는 2단계가 필요.
- 사용자 요구: "핫키와 마우스 동작 모두 가능".

### G4. **패널별 독립 단축키 부재(선택적)**
- 현재 `N`은 `active_panel` 하나만 다룸. A·B를 동시에 독립적으로 넘기려면
  `TAB → N → TAB → N` 이 필요. 마우스가 바로 그 해결책이므로 신규 키는
  필요 없다고 판단(아래 D1 참조).

---

## 3. 설계 결정

### D1. 마우스 바인딩: "마우스 커서 아래 패널" 타게팅

커서가 있는 패널의 시퀀스가 대상. `active_panel` 의존 없음 → A·B 각각 독립
탐색이 자연스럽게 해결됨.

**1차(권장):** 마우스 **사이드 버튼** — SDL 의 `SDL_BUTTON_X1` (Back) / `SDL_BUTTON_X2` (Forward).
- 의미 매핑이 직관적 ("뒤로/앞으로").
- 기존 left/right/middle/wheel 점유와 충돌 없음.
- ImGui 는 `ImGuiMouseButton_*` 에 X1/X2 상수(3, 4)가 있음. `ImGui::IsMouseClicked(3)` / `IsMouseClicked(4)` 로 접근 가능.

**2차(보조):** **Alt + Mouse Wheel** — 사이드 버튼 없는 트랙패드 사용자 위한 대안.
- 현재 휠은 줌에 사용 중, Alt 모디파이어로 구분.

두 가지 모두 `ImGui::IsItemHovered()` 로 대상 패널을 특정 → 해당 패널에만 적용.

### D2. 핫키: 기존 `N`/`Shift+N` 유지 + `PageDown`/`PageUp` 추가

- `N` / `Shift+N`: 기존 그대로(active_panel 기준).
- `PageDown` / `PageUp`: **신규 추가**, 동일 동작. 표준 네비게이션 키로
  발견성(discoverability) 개선. `Explore` 조사로 미사용 확인됨.

### D3. 파일명 토스트 오버레이

**신규 상태:**
```cpp
struct FilenameToastState {
    std::string filename;     // basename만 표시
    int         panel = 0;    // 0=A, 1=B (어느 패널의 로드인가)
    double      until = 0.0;  // ImGui::GetTime() 기반 만료 시각
};
FilenameToastState filename_toast;
```

**트리거:** `open_state` pending 처리 직후(`main_window.cpp:1228` 근처) +
CLI 로드 직후 + drop_file 로드 직후. 하나의 헬퍼 함수로 통합.

**렌더링:** `render_copy_mode_overlay` 패턴 재사용 — `GetForegroundDrawList()` +
`AddRectFilled`/`AddText`, 22px 폰트. **위치:** 해당 패널 상단 중앙(패널
rect 필요). 간단하게: 전역 상단 중앙 + "A:" / "B:" 접두사.

**표시 시간:** 1.5초 (copy 토스트와 동일 감각).

### D4. 공통 로드 헬퍼

G1·G2·G3 를 한 곳에서 해결하기 위해 `main.cpp` / `main_window.cpp` 양쪽에서
공용으로 쓸 수 있는 헬퍼:

```cpp
// src/image_loader.h (또는 app.h)
void load_image_and_populate_sequence(AppState& state,
                                      int panel,        // 0=A, 1=B
                                      const std::string& path);
```

- `free_image` + `load_image` + `scan_image_directory` + viewport 리셋 +
  `filename_toast` 업데이트 + diff 캐시 무효화를 한 번에.
- 기존 3군데 (CLI / drop / open_state) 모두 이 함수로 단일화 → 코드 중복 제거.

### D5. 네비게이션 실제 수행 함수

기존 `N` 키 핸들러의 로직을 분리 추출:

```cpp
// src/app.cpp (또는 전용 nav 모듈)
void sequence_navigate(AppState& state, int panel, int dir);  // dir = +1 / -1
```

- wrap-around, 빈 시퀀스 가드, `open_state` pending 트리거까지 포함.
- 호출자: `N`/`Shift+N` 키, `PageDown`/`PageUp` 키, 마우스 사이드 버튼, `Alt+Wheel`,
  슬라이드쇼(`main.cpp:440`) — 슬라이드쇼도 현재 수동 로직을 이 함수로
  갈아끼움.

---

## 4. 변경 파일 및 병렬 편집 그룹

병렬 실행 가능 단위로 묶음.

### Wave 1 (병렬 가능, 의존성 없음)

- **`src/app.h`**
  - `FilenameToastState` 추가 (AppState 멤버).
  - `sequence_navigate(AppState&, int panel, int dir)` 선언.

- **`src/image_loader.h`**
  - `load_image_and_populate_sequence(AppState&, int panel, const std::string& path)` 선언.

### Wave 2 (Wave 1 선언 의존)

- **`src/image_loader.cpp`**
  - `load_image_and_populate_sequence` 구현 — free + load + scan +
    viewport reset + diff 무효화 + `filename_toast` 갱신(1.5초).

- **`src/app.cpp`**
  - `sequence_navigate` 구현 추출.
  - `SDL_SCANCODE_N` 분기 → `sequence_navigate(state, state.active_panel, shift ? -1 : +1)` 로 치환.
  - `SDL_SCANCODE_PAGEDOWN`/`SDL_SCANCODE_PAGEUP` 신규 case 추가.

- **`src/main.cpp`**
  - CLI 로드 (`369-377`) → `load_image_and_populate_sequence(state, 0, cli.image_a)` / `(..., 1, cli.image_b)`.
  - `SDL_EVENT_DROP_FILE` (`411-419`) → 동일 헬퍼 사용.
  - 슬라이드쇼 advancement (`435-450`) → `sequence_navigate(state, state.slideshow.panel, +1)` 로 치환.
  - **마우스 사이드 버튼** 이벤트 처리 추가 — SDL `SDL_EVENT_MOUSE_BUTTON_DOWN` 에서 `button.button == SDL_BUTTON_X1/X2` 시, ImGui 의 hovered 판정은 `image_panel.cpp` 쪽이 적절하므로 실제 구현은 image_panel 쪽이 좋음(아래).

- **`src/ui/main_window.cpp`**
  - `open_state` 처리 블록(`1207-1231`) → `load_image_and_populate_sequence` 로 단일화.
  - 신규 `render_filename_toast(state)` 함수 — `render_copy_mode_overlay` 동일 스타일.
  - 메인 렌더 호출부에 `render_filename_toast(state)` 추가.
  - Help entries: "PageDown / PageUp" 및 "Mouse X1/X2, Alt+Wheel" 항목 추가 ("Sequence" 카테고리).

- **`src/ui/image_panel.cpp`**
  - 각 패널 렌더 루틴의 마우스 처리 블록에서:
    - `ImGui::IsItemHovered() && ImGui::IsMouseClicked(3)` → `sequence_navigate(state, panel_idx, -1)` (X1=Back=prev)
    - `... IsMouseClicked(4)` → `sequence_navigate(state, panel_idx, +1)` (X2=Forward=next)
    - `... && io.KeyAlt && io.MouseWheel != 0.0f` → wheel 부호로 +/-1.

### Wave 3 (의존: 기능 구현 완료)

- **`CMakeLists.txt`**: 신규 소스 파일 없음(기존 파일 편집만). 변경 불필요.

- **`doc_typst/av_cheat_sheet.typ`**: "Sequence Navigation" 섹션에
  PageDown/PageUp, Mouse X1/X2, Alt+Wheel, 파일명 토스트 설명 추가. v0.21 헤더.

- **`doc_typst/av-app-implementation.typ`**: "Phase 9: v0.21 디렉토리 네비게이션
  통합" 섹션 — G1~G3 버그 설명, 공용 헬퍼, 토스트, 마우스 바인딩 서술. 컴파일.

### Wave 4 (검증)

- `cmake --build build` 빌드.
- 수동 테스트:
  - `av a.png b.png` 실행 후 `N` → B 로딩 확인(active_panel 기본 0 이므로 TAB 후 N 도).
  - `av` 단독 실행 + drag-drop → N 으로 디렉토리 탐색.
  - A 디렉토리 ≠ B 디렉토리일 때 각 패널 독립 탐색.
  - wrap-around (마지막 → 처음) 동작.
  - 파일명 토스트 1.5초 표시.
  - 마우스 사이드 버튼(가능하면) / Alt+Wheel 테스트.
  - 슬라이드쇼 여전히 정상.

---

## 5. 엣지 케이스

- **빈 시퀀스** (`files.empty()` 또는 `current_index < 0`): no-op. 기존 N
  핸들러가 이미 처리.
- **단일 파일 디렉토리**: `% 1` → 항상 자기 자신 재로드. 토스트는 여전히
  뜨므로 사용자 피드백 OK.
- **로드 실패(깨진 파일)**: `load_image` false 반환 시 `filename_toast` 에
  "(failed)" 접두사 추가해 UX 일관성 유지. `images[panel].loaded = false`
  처리는 기존 `free_image`+`load_image` 조합에 맡김.
- **패널 교환(swap_images)**: `sequences[]` 는 "파일 슬롯 인덱스"(0/1) 기준이지
  "화면 좌/우 slot" 이 아님. `swap_images` 의 의미와 독립되어야 일관. → panel 인자는
  항상 data slot(0/1) 유지, UI 표시는 `swap_images` 를 고려해 "A:"/"B:" 라벨만 매핑.
- **재진입(빠른 연타)**: `filename_toast.until` 을 덮어쓰기만 하면 OK.

---

## 6. Help 엔트리 변경 미리보기

```
Sequence | N / PageDown             | Next image in directory sequence (active panel)
Sequence | Shift+N / PageUp         | Previous image in directory sequence (active panel)
Sequence | Mouse X1 (Back)          | Previous image (panel under cursor)
Sequence | Mouse X2 (Forward)       | Next image (panel under cursor)
Sequence | Alt + MouseWheel         | Navigate images (panel under cursor)
Sequence | A                        | Toggle slideshow auto-play
Sequence | Shift+Up / Shift+Down    | Slideshow interval ± 1s
```

---

## 7. 출력 산출물

1. 코드 변경(Wave 1-3)
2. 빌드 성공 + 수동 테스트 통과 로그
3. `doc_typst/av_cheat_sheet.typ` + `av-app-implementation.typ` PDF 재컴파일
4. 버전 bump: v0.20 → **v0.21**

---

## 8. 리스크

- **SDL 마우스 사이드 버튼**: 일부 트랙패드/노트북에서는 해당 버튼이 없음. Alt+Wheel 백업이 커버.
- **ImGui X1/X2 상수 값**: ImGui 버전에 따라 `ImGuiMouseButton_Count` 이상 접근 시 정의되지 않을 수 있음. 확인 후 `IsMouseClicked((ImGuiMouseButton)3)` 캐스팅으로 안전 처리. SDL 원시 이벤트에서 처리하는 것이 더 안전할 수도 있음 — 구현 시 실측 후 결정.
- **`sequence_navigate` 가 `open_state` 를 재사용**: 현재 한 프레임에 한 번만 pending 처리되므로 연타는 큐잉되지 않고 마지막 것만 적용됨. 사용자 경험상 OK(빠르게 N 연타 후 최종 이미지만 로드됨).

---

## 9. 승인 요청

위 설계로 진행해도 될지 확인 부탁. 특히:

- **(a)** 마우스 바인딩: 사이드 버튼(X1/X2) + Alt+Wheel 조합으로 OK?
- **(b)** 신규 핫키: `PageDown`/`PageUp` 추가 OK? (N/Shift+N 은 유지)
- **(c)** 파일명 토스트: 전역 상단 중앙에 "A: filename.png" 형식, 1.5초. OK?
- **(d)** 버전 bump v0.21 진행 OK?

승인 시 Wave 1-4 순서로 병렬 실행.
