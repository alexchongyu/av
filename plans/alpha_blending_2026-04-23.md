# Alpha Blending Diff Mode — Plan

**작성일:** 2026-04-23
**기능:** `Ctrl+2` 로 diff 패널에 A·B 알파블렌딩 이미지 표시. 블렌드 비율은 키로 1%씩 조절.

---

## 1. 조사 결과 핵심

| 자원 | 위치 | 상태 |
|---|---|---|
| `DiffState::Mode` enum (7개) | `src/app.h:38` | 확장 지점 |
| `Ctrl+2` 키 | — | **미사용** ✅ |
| `Ctrl+Left/Right` | — | **미사용** (fallthrough 가 ctrl 무시) |
| `[` / `]` | `src/app.cpp:418` | diff.amplify ±0.5 |
| GPU diff shader `u_diff_mode` | `src/shader_sources.h:74` | 0-4 사용, 5 추가 가능 |
| CPU `cpu_render_diff` | `src/ui/image_panel.cpp:320` | mode 분기 |
| `BLEND_FRAG_SRC` (A↔B mix) | `src/shader_sources.h:236` | 이미 존재(재사용 검토) |
| `OverlayState.alpha` | `src/app.h:192` | O 키 전용, 별개 유지 필요 |
| Diff 패널 렌더 조건 | `src/ui/main_window.cpp:1439` | A+B 둘 다 loaded |

---

## 2. 설계 결정

### D1. 새 mode는 `DiffState::Mode::AlphaBlend`

- 기존 Ctrl+N 패밀리와 일관 (사용자의 요구사항: "diff 영상 canvas에 표시").
- `OverlayState` 의 O 키 블렌드(A/B 패널을 *대체*) 와는 목적이 다름 — 이쪽은 diff 패널에만 표시.
- enum 뒤에 추가 (기존 순서 보존).

### D2. 블렌드 알고리즘

```
out.rgb = (1 - alpha) * A.rgb + alpha * B.rgb
out.a   = 1.0
```
`alpha` 범위 `[0.0, 1.0]`, 기본값 `0.5`.

**표시 의미:** `alpha=0.0` → A만, `alpha=1.0` → B만. 사용자에게는 "A: 50% / B: 50%" 같은 양방향 비율로 표기.

### D3. 알고리즘 구현 위치

**재사용 vs 확장:**
- `BLEND_FRAG_SRC` 는 이미 `mix(A, B, u_alpha)` 구현됨. 재사용하면 shader 코드 중복 없음.
- 다만 Overlay 용 shader는 `render_overlay()` 경로에 묶여 있어, diff 패널 렌더 경로(`render_diff`)에서 dispatch 하려면 pipeline 분기 필요.

**결정: DIFF_FRAG_SRC 에 mode=5 분기 추가 (중복 최소, 변경 국소화)**

- GPU: `shader_sources.h` DIFF_FRAG_SRC 에 3줄 추가(`else if (u_diff_mode == 5) { FragColor = vec4(mix(colA.rgb, colB.rgb, u_alpha), 1.0); }`).
- 새 uniform `u_alpha` (diff_engine.cpp 에서 `set_float("u_alpha", state.diff.alpha)`).
- CPU: `cpu_render_diff` 에 동일 분기 추가.

### D4. 키 매핑 결정 — **`[` / `]` 채택** (화살표 대신)

사용자 제안 비교:

| 후보 | 장점 | 단점 | 최종 |
|---|---|---|---|
| Left/Right 화살표 | 직관 ("좌우 슬라이드") | 팬(pan) 과 충돌 — 모드별 의미 전환 필요, 화살표 기본 의미가 방향이라 덜 컨텍스추얼 | ❌ |
| **`[` / `]`** | diff 파라미터 조절 컨벤션과 **완벽 일관**. 기존 `[`/`]` (amplify) 와 역할 동일 — "현재 diff 모드의 주 파라미터". 모드별 자동 전환. | amplify 와 기능 공유 — 모드마다 다른 의미. 하지만 AlphaBlend 모드에서 amplify 는 무의미(차분이 아니므로) 하므로 자연스러움. | ✅ |

**일관성 원칙:**
- `[` / `]` = "현재 diff 모드의 주 파라미터" (기존은 amplify, AlphaBlend 모드에서는 alpha)
- `\` = "주 파라미터 리셋" (기존은 amplify=1.0, AlphaBlend 모드에서는 alpha=0.5)
- `Shift+[` / `Shift+]` = threshold (Tolerance 모드 전용 — 기존 유지)

스텝 크기:
- `[` / `]`: ±0.01 (1%) — 사용자 요구 충족
- `Shift+[` / `Shift+]`: ±0.10 (10%) — 빠른 조절용 보너스 (기존에 Shift 조합이 threshold 였으므로 AlphaBlend 모드에서만 적용, 충돌 없음)

### D5. 현재 비율 시각 표시

사용자가 블렌드 비율을 항상 알 수 있도록 diff 패널 좌상단에 작은 텍스트 표시:

```
A: 40%  ━  B: 60%
```

- 기존 diff 패널의 info 텍스트 렌더 경로 재사용 (있다면) 또는 `ImGui::GetForegroundDrawList()` 작은 오버레이.
- AlphaBlend 모드에서만 표시. 모드 종료 시 자동 비표시.
- 폰트 22px, 배경 반투명 — copy_mode_overlay 패턴 재사용.

### D6. 단일 이미지 상황 처리

A만 로드된 상태에서 Ctrl+2 → diff 패널이 어차피 렌더되지 않음(A+B 둘 다 필요). 토글은 되지만 시각 효과 없음. **사용자 피드백이 필요** — 토스트로 "B image required" 같은 경고 1.5초.

### D7. DiffState 상태 확장

```cpp
struct DiffState {
    enum class Mode { None, PixelAbsolute, PixelRelative, Highlight,
                       FalseColor, SSIM, Enhance,
                       AlphaBlend /* NEW */ };
    Mode  mode    = Mode::None;
    float amplify = 1.0f;
    float alpha   = 0.5f;  // NEW: AlphaBlend mode ratio (0=A, 1=B)
    // ...기존 필드 유지
};
```

---

## 3. 변경 파일 · 병렬 그룹

### Wave 1 (독립 — 병렬)

- **`src/app.h`**: `DiffState::Mode::AlphaBlend` enum 추가, `DiffState::alpha` 필드 추가.
- **`src/shader_sources.h`**: DIFF_FRAG_SRC 에 `u_alpha` uniform 선언 + mode=5 분기 추가.

### Wave 2 (Wave 1 선언 의존)

- **`src/app.cpp`**:
  - Ctrl+1/2/3 switch 블록(라인 257-312)에 `key_num == 2` 분기 추가 → `state.diff.mode = (state.diff.mode == AlphaBlend) ? None : AlphaBlend`.
  - `[` `]` 키 블록(418-433) 분기: `if (state.diff.mode == AlphaBlend) state.diff.alpha ± 0.01` else `amplify ± 0.5`. `Shift+[/]` 는 AlphaBlend 모드일 때 alpha ±0.10, 아닐 때 기존 threshold.
  - `\` 키: AlphaBlend 모드면 alpha=0.5, 아니면 amplify=1.0 (기존). `Ctrl+\` 는 기존 threshold 리셋 유지.

- **`src/diff_engine.cpp`**:
  - `DiffRenderer::render()` switch 에 `case AlphaBlend: diff_mode_int = 5; break;` 추가.
  - `shader_.set_float("u_alpha", state.diff.alpha)` uniform 설정.

- **`src/ui/image_panel.cpp`**:
  - `cpu_render_diff()` mode 분기에 AlphaBlend 추가 — 단순 선형 블렌드. HDR/PPM/RGBA8 3가지 픽셀 경로 각각 처리.
  - `render_diff` 에서 AlphaBlend 모드일 때 좌상단 "A: X% ━ B: Y%" 오버레이 렌더 (작은 헬퍼 `render_alpha_hud` 신설).

- **`src/ui/main_window.cpp`**:
  - Help entries: "Ctrl+2" "Toggle Diff: Alpha Blend (A+B mix)" 추가.
  - `[`/`]` 엔트리 설명 업데이트: "Decrease/Increase primary param (amplify, or alpha in AlphaBlend mode, ±1%)".
  - 단일 이미지 상황 가드: Ctrl+2 후 B 미로드 시 1.5초 토스트 "B image required for Alpha Blend". (`filename_toast` 재사용 or 간단한 방식).

### Wave 3 (문서)

- **`doc_typst/av_cheat_sheet.typ`**: Diff 섹션에 Ctrl+2 추가, `[`/`]` 설명 업데이트, v0.22 헤더.
- **`doc_typst/av-app-implementation.typ`**: Phase 10: v0.22 Alpha Blend 섹션 추가. 컴파일.

### Wave 4 (검증)

- `ninja` 빌드.
- 수동 테스트:
  - `Ctrl+2` 로 토글 — diff 패널이 A+B 블렌드 표시.
  - `[` / `]` 로 alpha 1% 감소/증가 (좌상단 비율 표시 변경).
  - `\` 로 50% 리셋.
  - `Shift+[/]` 로 10% 큰 조절.
  - `Ctrl+2` 재토글 — None 복귀, `[` `]` 가 amplify 로 돌아감.
  - 단일 이미지일 때 Ctrl+2 → 경고 토스트 표시.

---

## 4. 엣지 케이스

- **A, B 크기 다름**: 기존 diff 처리와 동일하게 `min(w, h)` 영역만 블렌드. A/B 각각의 viewport.zoom·pan 을 따름 (기존 diff 렌더 로직이 이미 처리).
- **HDR (float32) vs LDR**: `cpu_render_diff` 기존 분기 그대로 유지. 각 경로에서 선형 mix.
- **Clamping**: alpha 는 0.0~1.0 경계 클램프. `\` 리셋 후 재조절 시 중간값으로 시작.
- **Mode 전환 시 alpha 유지**: AlphaBlend 켜고 → 다른 diff 모드로 전환 → 다시 AlphaBlend 로 돌아오면 이전 alpha 값 그대로 (state.diff.alpha 유지).

---

## 5. 버전 bump

v0.21 → **v0.22**

---

## 6. 승인 요청

위 설계로 진행해도 될지 확인:

- **(a)** DiffState::Mode::AlphaBlend 새 enum 추가 — `[`/`]` 컨텍스트 전환 방식. OK?
- **(b)** 키 매핑: `[`/`]` ±1%, `Shift+[`/`Shift+]` ±10%, `\` 리셋(0.5). OK? 또는 단순화 하여 `[`/`]` ±1% 만 유지?
- **(c)** 비율 표시: diff 패널 좌상단 "A: 40% ━ B: 60%" 오버레이. OK?
- **(d)** 버전 bump v0.22 OK?

승인 시 Wave 1-4 병렬 실행.
