# Histogram Y축 라벨 잘림 수정

## Context

Histogram 창에서 Y축 숫자(예: "1,234,567")가 왼쪽 margin 부족으로 잘린다.
현재 left margin이 **40px**로 하드코딩되어 있는데, comma-formatted 숫자는 최대 70-80px 폭이 필요하다.

## 근본 원인

`src/ui/chart_windows.cpp`에서 두 곳에 `40.0f`가 하드코딩:

- **line 115**: `float chart_w = std::max(avail_w - 16.0f - 40.0f, 200.0f);` — chart 폭 계산 시 왼쪽 margin 40px
- **line 157**: `ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);` — chart origin을 40px 오른쪽으로 이동

Y축 라벨은 `origin.x - text_width - 4px`에 그려지므로, 라벨 폭이 36px를 초과하면 잘린다.

## 수정 방안

`40.0f` → `80.0f`로 변경 (두 곳 모두). 80px이면 "12,345,678" (9자) 수준까지 충분히 수용 가능.

### 변경 파일: `src/ui/chart_windows.cpp`

1. **line 115**: `40.0f` → `80.0f`
2. **line 157**: `40.0f` → `80.0f`

## 검증

1. `cmake --build build` 성공
2. 앱 실행 후 Histogram 창에서 Y축 숫자가 잘리지 않는지 확인
