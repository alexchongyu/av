# av 기능 추가 계획 — `--pair` (파일명 페어링) + PSNR(info 창의 `p`)

- 작성일: 2026-07-19
- 원칙: **무회귀(no-regression)**. 기존 동작(특히 기존 `--sync` 뷰포트 동기화, `p` pathfinder)은 그대로 보존.
- 결정 사항: 새 파일명-페어링 기능의 플래그는 **`--pair`** (기존 `--sync`=뷰포트 동기화와 충돌 회피).

---

## 기능 1 — `--pair` : 두 디렉토리에서 "같은 파일명" 짝지어 비교

### 의미(사용자 사양)
- `av --pair <fileA> <dirB-or-fileB>`.
  - 패널 A(왼쪽) = `fileA` (그 디렉토리 = dirA).
  - 패널 B(오른쪽, 비교대상) = `dirB / basename(fileA)` — **A와 같은 파일명, 다른 디렉토리**.
  - 두 번째 인자는 파일이든 디렉토리든 허용: 디렉토리면 그대로 dirB, 파일이면 그 부모(parent)를 dirB로.
- **dirA == dirB 이면 경고 출력 후 종료** (비교 의미 없음).
- next/prev 탐색 시: **A가 마스터**. A가 dirA의 다음/이전 파일로 이동하면, B는 dirB에서 같은 파일명을 로드.
- B에 같은 이름이 **없으면**: A만 로드, B 패널에는 "매칭 영상 없음" 정보 표시.

### 설계

핵심 헬퍼 하나로 미러링 로직을 집중:
```
// image_loader.{h,cpp}
void pair_mirror_b(AppState& state, const std::string& a_path);
//  sib = state.pair_dir_b / basename(a_path)
//  존재+정규파일 → load_image_and_populate_sequence(state, 1, sib); panel_missing_msg[1] clear
//  아니면       → (B 로드돼 있으면 free_image) ; panel_missing_msg[1] = basename(a_path)
```

### 변경 파일/지점

1. **src/app.h**
   - `CliOptions`에 `bool pair = false;`
   - `AppState`에:
     - `bool  pair_mode = false;`  (런타임 페어 모드)
     - `std::string pair_dir_b;`   (비교 디렉토리 B, 고정)
     - `std::string panel_missing_msg[2];` (기본 "" — 매칭 없음 메시지)

2. **src/app.cpp**
   - `print_help()`: `--pair` 한 줄 추가.
   - `parse_cli()`: `else if (arg == "--pair") opts.pair = true;` (기존 unknown-flag 가드 앞).
   - `sequence_navigate()`: 함수 진입부에 `if (state.pair_mode) panel = 0;` **한 줄만** 추가(페어 모드는 항상 A가 구동). 그 외 로직 불변 → 일반 모드 회귀 없음.
   - (pair 검증/셋업은 main.cpp에서. apply_cli_options는 건드리지 않음.)

3. **src/main.cpp** (startup 로드 블록, 현재 두 파일 독립 로드)
   - `if (cli.pair) { …검증+페어 로드… } else { …기존 코드 그대로… }` 로 분기.
   - 검증: 두 인자 필수 / `dirB = is_directory(image_b) ? image_b : parent_path(image_b)` / `weakly_canonical(dirA)==weakly_canonical(dirB)` 이면 stderr 경고 + `return 1`.
   - 성공 시 `state.pair_mode=true; state.pair_dir_b=dirB;` → A 로드 → `pair_mirror_b(state, cli.image_a)`.
   - **else 분기는 기존 코드와 바이트 동일** (회귀 0).

4. **src/image_loader.cpp**
   - `pair_mirror_b()` 신설.
   - 성공 커밋 지점(현 psnr_computed 리셋 근처, ~line 452)에 두 줄:
     - `state.panel_missing_msg[panel].clear();`  (로드 성공 시 해당 패널 missing 해제)
     - `state.info_psnr_computed = false;`         (아래 기능 2용 — 영상 바뀌면 PSNR 무효화)

5. **src/ui/main_window.cpp** (deferred open 처리, ~1240-1254)
   - 기존 `load_image_and_populate_sequence(state, target, opened_path)` 를 `bool ok = …` 로 받고,
   - 직후: `if (ok && state.pair_mode && target == 0) pair_mirror_b(state, <loaded A path>);`
   - (opened_path clear 전에 경로 캡처)

6. **src/ui/image_panel.cpp** (`render_single`, line 1815-1818 빈 패널)
   - `(no image)` 자리에서: `panel_missing_msg[actual_idx]` 비어있지 않으면
     `"⚠ No matching image:\n<name>\nin <dirB>"` 를 표시. 비어있으면 기존 `"(no image)"`.
   - `render_single_software`의 빈 패널 경로(~1916)도 동일 처리.

### 회귀 안전성
- 일반 모드에서 `pair_mode=false` → sequence_navigate/processor/render 분기 모두 기존 경로. 신규 필드는 기본값 "".
- INI 저장/로드에 pair는 넣지 않음(런치 타임 모드).

---

## 기능 2 — info 창에서 `p` → PSNR (A=기준/왼쪽, B=비교/오른쪽)

### 재사용
- `compute_diff_stats(imgA, imgB, extra)` (chart_export.cpp:166) — 이미 채널별 PSNR 계산, A/B 순서 일치.
- 색 임계값은 statusbar.cpp:80-93 재사용(>40 녹색, >30 노랑, else 빨강).

### 설계
- **`p` 키 충돌 회피**: `show_info && images[0].loaded && images[1].loaded` 일 때만 PSNR 계산 후 `break`.
  그 외에는 **기존 pathfinder 동작 그대로** → 회귀 없음.
- **전용 필드** 사용(기존 diff-mode 자동 PSNR의 `diff.psnr_db`는 매 프레임 리셋되므로 충돌 회피):
  - `bool  info_psnr_computed = false;`
  - `float info_psnr_db = -1.0f;`  (999=inf/identical)
  - `bool  info_psnr_mismatch = false;`  (크기/포맷 불일치)

### 변경 파일/지점
1. **src/app.h** — `AppState`에 위 3개 필드.
2. **src/app.cpp**
   - `#include "chart_export.h"` 추가.
   - `case SDL_SCANCODE_P:` 를 재구성:
     ```
     if (!ctrl && !gui && state.show_info &&
         state.images[0].loaded && state.images[1].loaded) {
         // 크기 불일치 or 포맷 불일치(one hdr one ldr) 가드
         bool bothU8  = !A.pixels.empty()     && !B.pixels.empty();
         bool bothF32 = !A.pixels_f32.empty() && !B.pixels_f32.empty();
         if (A.w!=B.w || A.h!=B.h || (!bothU8 && !bothF32)) {
             state.info_psnr_mismatch = true;
         } else {
             DiffExtraStats extra; compute_diff_stats(A,B,extra);
             double sum=0; int cnt=0;
             for(c<3) if(psnr[c]>0 && psnr[c]<999){sum+=;cnt++;}
             state.info_psnr_db = cnt? sum/cnt : 999.0f;
             state.info_psnr_mismatch = false;
         }
         state.info_psnr_computed = true;
         break;   // p 소비 — pathfinder로 안 감
     }
     // 기존 pathfinder 로직 그대로
     ```
   - 리셋: 영상 로드 시 `info_psnr_computed=false` (기능1의 image_loader.cpp 변경에 포함).
3. **src/ui/main_window.cpp** (Image Info 창, 1513-1551 Separator 뒤)
   - `info_psnr_computed` 이면 PSNR 줄 표시:
     - mismatch → 빨강 `"PSNR: N/A (size/format mismatch)"`
     - >=999 → 녹색 `"PSNR: inf (identical)"`
     - else → 임계값 색 `"PSNR: %.2f dB   [A(Left)=ref, B(Right)=cmp]"`
   - 아직 계산 안 했고 둘 다 로드된 경우: `TextDisabled("PSNR: press 'p' to compute")` (발견성).

### 회귀 안전성
- `p`는 info 창+양쪽 로드 조건에서만 가로챔. 그 외 pathfinder 100% 보존.
- diff-mode 자동 PSNR(`diff.psnr_db`, statusbar)과 **완전히 분리된 필드** 사용 → 상호 간섭 0.

---

## 빌드/검증 계획
1. `/opt/homebrew/bin/cmake --build build` (cmake alias 주의: `-G Ninja`).
2. CLI 파서 검증: `av --help` 에 `--pair` 표기 / `av --pair a b` 동작 / 같은 dir 경고+종료.
3. PSNR 산술 검증: 동일 이미지 → inf, 알려진 차이 이미지 → 손계산과 대조(chart export CSV와 교차확인 가능).
4. 무회귀: 일반 모드(비-pair) 실행 시 next/prev·`p` pathfinder·기존 `--sync` 동작 불변 확인.
5. GUI 상호작용(패널 missing 표시, info 창 PSNR 줄)은 헤드리스 한계 → 코드 경로 증명 + 빌드로 확인, 최종 확인은 사용자 실행.

## 커밋 계획 (원자적, finding 단위)
- C1: `feat: --pair mode (filename pairing across two dirs)` — 기능1 전체.
- C2: `feat: PSNR readout in image-info overlay (press p)` — 기능2 전체.
- (푸시는 사용자 승인 후에만.)
