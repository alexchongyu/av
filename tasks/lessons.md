# Lessons Learned — av 프로젝트

## 구조체 이동 시 anonymous namespace 처리

**상황**: chart_windows.cpp의 anonymous namespace 안에 정의된 구조체와 static 함수들을
chart_export.h/cpp로 이동할 때 발생하는 중복 정의 문제.

**해결책**:
- 구조체는 chart_export.h에 global namespace에 정의
- chart_windows.cpp의 anonymous namespace에서 struct 정의 + static compute 함수 제거
- draw_stats_table 함수만 anonymous namespace에 유지 (render_stats_window에서 호출)
- chart_export.h를 chart_windows.cpp 상단에 include

## stb 헤더 구현 파일 (stb_impl.cpp) 패턴

- `STB_TRUETYPE_IMPLEMENTATION`은 반드시 stb_impl.cpp에 한 곳에서만 정의
- soft_renderer.cpp에서는 `#include <stb_truetype.h>`만 (IMPLEMENTATION 없이)
- stb_image_write.h는 이미 stb_impl.cpp에 있으므로 soft_renderer.cpp에서 include 가능

## SDL 파일 다이얼로그 패턴

- `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`는 비동기 콜백 방식
- userdata struct를 `new`로 heap 할당하고, 콜백에서 `delete`
- 콜백 내에서만 pending_path 설정, 실제 처리는 다음 프레임 render() 시작에서

## ImGui PushFont/PopFont 위치

- `ImGui::Begin()`이 실패해도 (창이 접힌 경우) PopFont는 필요
- 패턴: `PushFont` → `Begin` → (content) → `End` → `PopFont` (Begin 결과와 무관하게)

## CMakeLists.txt 소스 파일 추가

- 새 .cpp 파일은 AV_SOURCES에 반드시 추가
- 타겟 컴파일 정의 (IMGUI_FONT_DIR 등)는 av 타겟에 연결된 모든 소스에서 사용 가능

## 차트 Export 버튼 중복 ID 방지

- ImGui SmallButton 등은 동일한 레이블을 가지면 ID 충돌 발생
- `##suffix` 형식으로 고유 ID 지정 필수 (예: `"PNG A##ha"`, `"PNG B##hb"`)

## 새 기능 추가 패턴 (2026-03-05)

### AppState 확장 패턴
- 새 state 구조체를 app.h에 정의 후 AppState에 필드로 추가
- 구조체에 기본값 지정 (`bool active = false;` 등)

### 새 분석 창 추가 패턴
1. `chart_export.h/cpp`에 데이터 구조체 + 추출 함수 추가
2. `chart_windows.h`에 render 함수 선언
3. `chart_windows.cpp`에 구현 (draw_stats_table 재활용 가능)
4. `app.h` AppState에 `show_xxx` bool 추가
5. `app.cpp` handle_keyboard에 단축키 추가
6. `main_window.cpp` 메뉴에 MenuItem 추가 + render 함수 호출

### 새 렌더링 모드 추가 패턴 (Overlay/Blend)
1. `shader_sources.h`에 새 GLSL 셰이더 추가
2. `image_panel.h`에 새 ShaderProgram 멤버 + render 메서드 선언
3. `image_panel.cpp` init()에서 새 셰이더 컴파일
4. `image_panel.cpp` render()에서 조건부로 새 렌더 메서드 호출
5. `main_window.cpp` 레이아웃 분기에 새 모드 추가

### ROI 선택 패턴
- ROI 모드 ON → left drag에서 pan 대신 ROI 선택
- 드래그 완료 시 화면 좌표 → 이미지 픽셀 좌표 변환 (기존 s2ix/s2iy 패턴)
- ROI 오버레이: 이미지 픽셀 → 화면 좌표 역변환으로 사각형 그리기

### 시퀀스 탐색 패턴
- 이미지 로드 시 자동으로 `scan_image_directory()` 호출 → SequenceState 갱신
- N/Shift+N 키로 current_index 변경 → open_state.open_pending 설정
- main_window.cpp의 open_pending 처리에서 자동으로 로드 + 시퀀스 재스캔

### open_state 재사용 패턴
- 파일 다이얼로그뿐 아니라 시퀀스 탐색에서도 open_state.opened_path/open_pending 사용 가능
- clear_other=false 설정으로 다른 슬롯 보존
