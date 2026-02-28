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
