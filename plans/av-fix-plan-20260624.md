# av 개선 구현 계획 (2026-06-24)

> 멀티 에이전트 분석(11) + 실제 코드 검증(12) 산출물 기반.
> 우선순위: **① 치명 버그 → ② 성능 → ③ 리팩터링 → ④ 신기능(마지막)**
> 절대 원칙: **No regression / 새 버그 금지** — 자세한 방법론은 `tasks/lessons.md` 참조.

## 0. 무회귀 게이트 (모든 커밋 공통)

1. **베이스라인**: 작업 시작 전 증분 빌드 성공 확인 + `test/` 이미지로 골든 export(PNG/CSV) 캡처.
2. **원자적 커밋**: 항목 1개 = 커밋 1개. 회귀 시 bisect 격리.
3. **변경 후 게이트**: 빌드 무경고 → 해당 동작 재현 → 정상(happy-path) 케이스 무손상 확인 → 커밋.
4. **리팩터링은 출력 픽셀 동일**(앱 export로 before/after 바이트 대조). behavior-change와 절대 분리.
5. 푸시는 **사용자 승인 후에만**.

---

## Phase A — 치명 버그 (6건 · 전부 low risk)

### A1. SSIM 워커→메인 데이터 레이스 (UB) — trivial
- **위치**: `src/ui/main_window.cpp:587-588`(non-atomic 글로벌), `:1346-1348`(워커 스레드 write), `:1287`(메인 read); 생산자 `src/diff_engine.cpp:328`.
- **근본원인**: `s_ssim_ready`(bool)/`s_ssim_result`(SSIMResult=vector+ints)를 동기화 없이 스레드 간 게시 → torn read/재정렬 UB.
- **수정**: `s_ssim_ready`를 `std::atomic<bool>`로. 생산자 `store(true, release)`(결과 write 후), 소비자 `load(acquire)` 후 읽기. 새 include 불필요(`<atomic>` 전이 포함). 단일 생산자/소비자라 mutex 불필요(compute()가 cancel→join 선행).
- **검증**: TSan 빌드로 fix 전 race 검출/후 clean. SSIM 모드 반복 토글 시 heatmap/score 정상, 크래시 없음.

### A2. 로드 실패 시 기존 이미지 파괴 (destructive) — small
- **위치**: `src/image_loader.cpp:409-414`(무조건 free 후 load), `:312`(`entry={}`), `:416-425`(viewport/seq 무조건 갱신).
- **근본원인**: 새 로드 성공 여부 확인 전에 기존 슬롯을 free + `entry={}` → 실패 시 패널 영구 소실(특히 듀얼 패널 비교 파괴적).
- **수정**: `load_image_and_populate_sequence`만 수정. 임시 `ImageEntry tmp`에 로드 → 성공 시에만 `free_image(old)` + `move`. 실패 시 부분 텍스처 누수 방지(`free_image(tmp)` if `texture_id`), viewport/PSNR/sequence 갱신을 `if(ok)`로 가드. toast.failed는 무조건 유지. `load_image`의 `entry={}`는 **건드리지 않음**(타 호출부 의존).
- **검증**: 정상 이미지 표시 후 잘못된 경로 로드(CLI/드래그/다이얼로그/시퀀스) → fix 후 기존 이미지·viewport·seq 인덱스 유지 + failed toast. happy-path 무회귀. 텍스처-업로드-실패 분기 반복 시 누수 없음.

### A3. PPM ASCII(P3) text 모드 → Windows CRLF 버그 — trivial
- **위치**: `src/image_save.cpp:85` (`binary ? "wb" : "w"`).
- **근본원인**: 바로 위 주석과 모순. P3 경로가 `"w"`(text) → Windows에서 `\n`→`\r\n` 변환.
- **수정**: `std::fopen(path, "wb")`로 고정(P6는 이미 정상). P3 내용은 명시적 `\n`이라 유효.
- **검증**: Windows hex로 0x0A만 확인(fix 전 0x0D0A). POSIX는 byte-identical(무회귀). P6 변경 없음.

### A4. 소프트 모드 AlphaBlend가 alpha 무시(50/50 고정) — trivial
- **위치**: `src/ui/image_panel.cpp:742-744`(`cpu_render_diff` 호출에 alpha 미전달); GPU 경로 `:2084`는 전달.
- **근본원인**: `cpu_render_diff`의 `alpha`(default 0.5) 미전달 → 소프트 모드에서 슬라이더 무효.
- **수정**: 호출에 `state.diff.alpha` 추가(시그니처 변경 없음). ⚠️ 소프트 모드 출력이 바뀌는 **의도된 버그 수정**(회귀 아님) — 단독 커밋.
- **검증**: 소프트 모드 + AlphaBlend(Ctrl+2), `[`/`]`로 alpha 극단 → A/B 정확히 전환, GPU 경로와 일치.

### A5. av-x11 32-bit int 프레임버퍼 오버플로우 — small
- **위치**: `src/av_x11.c:156-157`(int 곱), `:231/262/392/424`(행 오프셋 int), `:463-470`(ConfigureNotify 무검증).
- **근본원인**: `win_w*win_h*bpp`가 int로 계산→큰 창에서 wrap→calloc(0)/음수→OOB write. WM/공격자 제어 가능.
- **수정**: (1) size를 `(size_t)w*(size_t)h*(size_t)bpp`로, (2) 4개 행 오프셋 size_t로, (3) `av_resize_framebuf` 진입부에 dim 클램프(1..16384) — 이 트리오가 핵심. calloc 반환 검증.
- **검증**: ASan으로 32768² 강제 시 fix 전 heap-overflow/후 clean. 일반 리사이즈/맥시마이즈 렌더 정상(무회귀).

### A6. diff 모드 집합 5곳 불일치(메뉴/help/statusbar/title/CLI) — small
- **위치**: `app.h:38-47`(enum 8개); menu `main_window.cpp:722-738`, title `:36-44`, statusbar `:6-17`, CLI `app.cpp:97-106`.
- **근본원인**: 단일 진실원 부재. AlphaBlend/Enhance가 메뉴·title·statusbar·CLI에 누락(메뉴 도달 불가, statusbar `?`), Highlight 핫키 라벨 오류(Ctrl+5→실제 Ctrl+9).
- **수정**: `app.h`에 `constexpr DiffModeInfo kDiffModes[]`{mode,display,short_tag,hotkey,cli_token} 추가 → statusbar/title/menu/CLI를 전부 이 테이블 루프로 구동. (대안: 누락 case만 추가하는 최소 수정도 가능하나 테이블이 재발 방지로 권장.)
- **검증**: Ctrl+2~9 전 모드 순회 시 statusbar/title/menu 체크마크 일치, `?`/빈 태그 없음. `--diff-mode alphablend` 동작, `--help`에 표기.

---

## Phase B — 성능 (3건)

### B3. 콘텐츠 버전 스탬프 → dim-only 캐시 무효화 (먼저: 인프라) — small
- **위치**: `image_panel.cpp:454-456`(diff-listing 캐시 dim-only), `chart_windows.cpp:1262`(scatter 동일), `main_window.cpp:1340`(SSIM 모드 전환만 게이트).
- **근본원인**: 캐시 키가 (w,h)뿐 → 동일 크기 프레임 내비/정사각 회전 시 stale.
- **수정**: `ImageEntry`에 `uint64_t content_version`, `AppState`에 `content_version_counter`. 로드 funnel(`:414`)·회전(`app.cpp:508`)에서 bump. diff-listing/scatter/SSIM 캐시 키를 version으로 교체.
- **검증**: 동일 크기 프레임 전환/정사각 회전 시 listing/Identical/scatter/SSIM이 갱신됨. 헤드리스: 같은 WxH 두 entry, in-place 변경+bump 후 재계산 결과 반영.

### B2. 소프트 diff 패스 융합 (⚠️ 캐싱 아님) — small
- **위치**: `image_panel.cpp:742-760`(render_diff_software), `count_nonzero_diff_pixels:591`, `apply_threshold_to_diff:631`.
- **근본원인**: Highlight+threshold 시 프레임당 2~3회 전체 픽셀 재계산(각 bilinear 2회). **뷰포트 종속**이라 캐싱 불가(stale 위험=새 버그).
- **수정**: `cpu_render_diff` 단일 루프에서 Highlight count + threshold를 함께 산출(out 파라미터 추가) → 3패스를 1패스로. (최소 변형: pass 2+3만 1개 helper로 병합 = 3→2.) 출력은 byte-identical 보장.
- **검증**: 융합 출력이 기존 count_nonzero/apply_threshold 결과와 byte-identical(임시 assert). pan/zoom 시 매 프레임 갱신(캐싱 오도입 없음). sample_pixel 호출 ~2-3배 감소.

### B1. dead ImageCache 연결 + 프리페치 (perf 중 유일 medium) — medium
- **위치**: `image_loader.cpp:600-642`(완성됐으나 무호출), `:414`(funnel이 직접 load).
- **근본원인**: 시퀀스 내비/슬라이드쇼가 매번 디스크 재디코드+재업로드.
- **수정**: **copy-out** 방식(캐시가 텍스처 소유 유지, 표시 슬롯은 독립 텍스처 복사) → double-free 회피. `load_image_cached` helper: hit 시 CPU 버퍼 deep-copy + 새 텍스처 업로드(`upload_entry_texture`로 tail 추출), miss 시 기존 load. 선택적 ±1 프리페치(동기, 가드). **비소유 포인터 방식은 회전/diff가 live 슬롯을 in-place 변경하므로 기각**(공유 캐시 손상 위험).
- **검증**: load 진입 trace로 재방문 시 디코드 없음(≤8). cache-hit 재방문이 픽셀 동일(HDR/PPM>255의 pixels_f32/orig deep-copy 검증). >8 내비 후 GL 에러/누수 없음. **회전 회귀**: cache-hit 후 회전→이탈→복귀 시 캐시 복사본 un-rotated 유지.

---

## Phase C — 리팩터링 (3건 · behavior-preserving)

### C2. 화면 차트가 chart_export extractor 호출 — small
- **위치**: `chart_windows.cpp:300`(diff-histogram에 잉여 `is_hdr` 게이트), `:256-335/600-684/851-936`(inline 재구현) vs `chart_export.cpp` extract_*.
- **Part A(정정, 먼저/단독)**: `:300` 게이트를 `!pixels_f32.empty()`로 → 고비트 PPM(maxval>255, is_hdr=false) diff에서 화면==export 일치. **1줄, 회귀면 최소.**
- **Part B(중복 제거, 선택)**: 3개 창이 extract_*를 호출하고 반환 struct를 그리도록. draw_hist/line_section 프레젠테이션은 유지.
- **검증**: 16-bit PPM A/B로 화면 diff 히스토그램 == export CSV(Part A 전 불일치). 일반 8-bit/HDR는 시각적 무변화. Part B는 8-bit 페어로 pre/post 픽셀 동일.

### C3. 저장 다이얼로그 중복 정리 (범위 축소) — medium
- **위치**: `image_save.cpp:231-243`·`266-278`(format switch 중복), `main_window.cpp` 3개 다이얼로그 boilerplate.
- **수정**: (1) `image_save.cpp`에 `save_image_format()` static helper로 switch 단일화(~24줄 제거, 무동작변경). (2) 창/상태/푸터 boilerplate를 작은 helper 3~4개로(상태 메시지 불일치 1곳서 해소). **통합 단일 다이얼로그는 시도 안 함**(per-item 구조 상이 → 콜백 폭증, 가독성 악화).
- **검증**: Step 1은 구조상 무변경 — PNG/BMP/PPM(8/16-bit) 저장 파일을 pre-change와 byte-compare. Step 2는 3개 다이얼로그 Save/Save-All/닫기 동작 동일.

### C1. 좌표 변환 통합 (가장 큼/주의 · 마지막) — medium
- **위치**: `image_panel.cpp` ~20곳(forward 12 + inverse 8) + `main_window.cpp:1652/1715` balloon. natural home `viewport.h/.cpp`는 helper 0.
- **수정 단계**:
  - **Step 1**: `viewport.h`에 순수 inline `vp_screen_to_image_x/y`, `vp_image_to_screen_x/y`(ImVec2-free).
  - **Step 2(⚠️ behavior-change, 별도 커밋·승인 필요)**: `(int)` 절단 사이트(`:152-153`, `:270-271/297-298`)를 `std::floor`로 표준화 → 가장자리 1px 정합. 이건 **출력 변경**이라 리팩터링과 분리.
  - **Step 3**: inline 복사본을 함수 1개=커밋 1개로 교체. 이미 람다화된 사이트(s2ix/s2iy `:1140/2464`, ix2s/iy2s `:2503`)부터. 각 커밋마다 소프트 출력 버퍼 byte-compare.
  - **Step 4**: 내부 루프 5곳(`140/332/591/631/677`) 마지막, per-pixel 오버헤드 벤치.
- **검증**: Step 2는 top-left 경계서 magnifier/crosshair 리드아웃==렌더 픽셀 일치. Step 3/4는 각 사이트 소프트 출력 byte-identical. double-click/right-drag zoom/ROI/pathfinder/balloon 좌표 무회귀.

---

## Phase D — 신기능 (Phase A~C 완료 후, 별도 승인)

후보(분석 산출): 새 diff 모드(log/edge), 새 입력 포맷(TIFF/EXR), JPEG 저장, 라인컷 위치 선택, 분석창 배타 enum화.
→ A~C로 코드가 정리·안정화된 뒤 착수(특히 신 diff 모드/포맷은 중복 지점 정리 후가 안전).

---

## 권장 실행 순서

A1 → A3 → A4 → A6 → A2 → A5  (버그; trivial→small)
→ B3 → B2 → B1  (성능; low→medium)
→ C2(Part A) → C2(Part B) → C3 → C1(Step1→2→3→4)  (리팩터링; 승인 게이트는 C1 Step2)
→ D (재승인)
