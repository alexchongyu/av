# Suppress "Identical" Overlay in AlphaBlend Mode — Plan

**작성일:** 2026-04-23
**버그:** Ctrl+2 (AlphaBlend diff 모드) 에서 A·B 가 동일하면 "Identical" 오버레이가 떠서 블렌드 화면을 가린다. AlphaBlend 모드는 차이 가시화가 목적이 아니므로 의미 없음.

## 원인

"Identical" 오버레이 렌더 조건: `state.diff_listing.identical && state.diff.mode != DiffState::Mode::None` — 즉 **None 이 아닌 모든 diff 모드**에서 표시. AlphaBlend 도 포함되어 버림.

## 위치

`src/ui/image_panel.cpp` 의 두 지점 (동일 패턴):

- **line 800** — 일반 패널(A/B) 렌더 경로. `if (ch_identical && state.diff.mode != DiffState::Mode::None) {`
- **line ~2157** — diff 패널 렌더 경로(`render_diff`). 동일 패턴.

## 수정

조건에 `&& state.diff.mode != DiffState::Mode::AlphaBlend` 추가. 2군데 모두.

```cpp
if (ch_identical
    && state.diff.mode != DiffState::Mode::None
    && state.diff.mode != DiffState::Mode::AlphaBlend) { ... }
```

## 이유

AlphaBlend 는 "두 이미지의 *혼합*" 을 관찰하는 모드이므로 Identical 판정 자체가 UX에 불필요. 동일 이미지 blend 는 결과도 동일 이미지이므로 Identical 라벨은 중복 정보이면서 HUD(좌상단 비율)를 가릴 수 있음.

## 테스트

- 동일한 파일을 A, B 로 로드 → Ctrl+2 → Identical 오버레이 표시 안 됨. 좌상단 HUD 만 보임.
- Ctrl+3 (PixelAbsolute) 로 전환 → Identical 오버레이 정상 표시.

## 리스크

없음. 조건 한 개 추가, 기존 동작 변경 없음.

## 승인 요청

위와 같이 2줄 수정으로 진행. OK?
