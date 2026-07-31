// ─────────────────────────────────────────────────────
// av — Cheat Sheet
// ─────────────────────────────────────────────────────

#import "_version.typ": AV_VERSION, AV_VERSION_DATE

#set document(
  title: "av — Cheat Sheet",
  author: "Alex",
  date: datetime(year: 2026, month: 3, day: 13),
)

#set page(
  paper: "a4",
  margin: (top: 1.5cm, bottom: 1.5cm, left: 1.2cm, right: 1.2cm),
  numbering: none,
)

#set text(
  // CMU Serif for Latin; Korean → first available CJK font. NOTE: an
  // unresolvable name in this list breaks typst's fallback chain, so only list
  // fonts actually installed here (Noto Sans CJK KR is not present on this Mac).
  font: ("CMU Serif", "Apple SD Gothic Neo", "NanumGothic"),
  size: 8.5pt,
  lang: "ko",
)

#set par(
  justify: false,
  leading: 0.52em,
  spacing: 0.5em,
)

// ── 인라인 코드 ──
#show raw.where(block: false): it => box(
  fill: luma(240),
  inset: (x: 3pt, y: 1.5pt),
  radius: 2pt,
  text(size: 7.5pt, font: "CMU Typewriter Text", it),
)

// ── 코드 블록 ──
#show raw.where(block: true): it => block(
  width: 100%,
  fill: luma(245),
  inset: (x: 0.6em, y: 0.4em),
  radius: 3pt,
  stroke: 0.3pt + luma(200),
  text(size: 7pt, font: "CMU Typewriter Text", it),
)

// ── 섹션 헤더 ──
#let section(title) = {
  v(0.3em)
  block(
    width: 100%,
    fill: luma(60),
    inset: (x: 0.5em, y: 0.25em),
    radius: 2pt,
    text(size: 9pt, weight: "bold", fill: white, title),
  )
  v(0.2em)
}

// ── 키 표시 헬퍼 ──
#let key(k) = box(
  fill: luma(230),
  inset: (x: 3pt, y: 1.5pt),
  radius: 2pt,
  stroke: 0.3pt + luma(180),
  text(size: 7.5pt, font: "CMU Typewriter Text", k),
)

// ──────────────────────────────────────────────────────
// 타이틀
// ──────────────────────────────────────────────────────

#align(center)[
  #text(size: 20pt, weight: "bold")[av #text(size: 12pt, weight: "regular", fill: luma(80))[\u{2014} Advanced Pixel Lens Cheat Sheet]]
  #v(0.15em)
  #text(size: 8pt, fill: luma(120))[#AV_VERSION \u{2014} #AV_VERSION_DATE]
]

#v(0.3em)

// ──────────────────────────────────────────────────────
// 2\-column 레이아웃
// ──────────────────────────────────────────────────────

#columns(2, gutter: 1em)[

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 1: CLI
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("CLI 사용법")

```
av [image_a] [image_b] [options]
```

#table(
  columns: (1fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 2pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [`--diff-mode <mode>`],   [abs \| rel \| falsecolor \| ssim],
  [`--zoom <level>`],       [fit \| 1 \| 2 \| ...],
  [`--amplify <val>`],      [0.1\~100 (diff 증폭)],
  [`--sync` / `--no-sync`], [패널 줌/팬 동기화],
  [`--fullscreen`],         [전체 화면 시작],
  [`--geometry <WxH>`],     [창 크기 지정],
  [`--software`],           [소프트웨어 렌더러 사용],
  [`--windowed`],           [윈도우 모드 (타이틀바 표시)],
  [`-nb`],                  [테두리 숨김 상태로 시작],
  [`--profile <icc>`],      [ICC 프로파일 적용],
  [`--no-color-mgmt`],      [색상 관리 비활성화],
  [`--lut <file.cube>`],    [3D LUT 룩 적용 (#key("'") 토글)],
  [`--cdl <file.cdl>`],     [ASC\-CDL slope/offset/power+sat 룩],
  [`-p <N>`],               [팬 이동 단위 (픽셀)],
  [`-bc <A> <B> <D>`],      [패널 테두리 색상 (hex)],
  [`--comp i1 i2 i3`],      [3\-영상 블록 비교: 원본 \| img2 \| img3 (diff 비활성)],
  [`--blk <WxH>`],          [`--comp` 블록 크기 (기본 8x8)],
  [`--num_blk <N>`],        [`--comp` worst 블록 표시 개수 (기본 16)],
  [`--comp-out <f|->`],     [헤드리스: 블록별 PSNR 테이블 CSV/JSON 출력 후 종료],
  [`--blk-metric <m>`],     [worst 랭킹 기준 rgb\|y\|chroma (75% + 피크픽셀 25% 혼합)],
  [`--comp-batch`],         [헤드리스: 3개 dir 전체 프레임별 comp 요약 CSV],
  [`--grid [WxH]`],         [시작 시 블록 그리드 표시 (셀 크기 기본 16x16, #key("G") 토글)],
)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 2: 키보드 단축키
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("키보드 단축키")

*줌*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("+") / #key("-")],        [줌 인 / 줌 아웃],
  [#key("Z") / #key("Shift+Z")], [줌 인 / 줌 아웃 (대체)],
  [#key("X")],                   [줌 아웃 (#key("Z")/#key("X") 쌍)],
  [#key("0")],                    [Fit to window],
  [#key("1")\~#key("8")],        [2#super[n] 배율 (1×\~256×)],
  [#key("F")],                    [Fit 토글],
  [#key("Space")],                [1:1 (100%) 토글],
)

*이동*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("h") #key("j") #key("k") #key("l")], [좌 / 하 / 상 / 우],
  [#key("←") #key("↓") #key("↑") #key("→")], [방향키 이동],
  [#key("Shift") + 이동],                      [빠른 이동 (×5)],
  [#key("Cmd+Shift") + #key("H") #key("J") #key("K") #key("L")], [각 방향 끝단으로 이동],
  [#key("G")],                                  [이미지 중심으로],
)

*비교 (Diff 모드)*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Ctrl+D")], [None (diff 끄기)],
  [#key("Ctrl+0")], [FLIP 지각 오차맵 (magma)],
  [#key("Ctrl+1")], [Signed 부호차 (파랑=A\<B / 빨강=A>B)],
  [#key("Ctrl+2")], [Alpha Blend (A·B 혼합)],
  [#key("Ctrl+3")], [Absolute diff],
  [#key("Ctrl+4")], [Relative diff],
  [#key("Ctrl+5")], [Enhance (remap)],
  [#key("Ctrl+6")], [False color],
  [#key("Ctrl+7")], [SSIM],
  [#key("Ctrl+8")], [Tolerance diff 토글],
  [#key("Ctrl+9")], [Highlight],
  [#key("Ctrl+\\")], [Threshold 리셋],
)

*Diff 주 파라미터 (모드별)*
#table(
  columns: (1.2fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("[") / #key("]")],           [증폭 \u{2212}0.5 / +0.5 (AlphaBlend: alpha \u{2213}1%)],
  [#key("Shift+[") / #key("Shift+]")], [Threshold \u{2213}1 (AlphaBlend: alpha \u{2213}10%)],
  [#key("\\")],                      [증폭 리셋 (AlphaBlend: alpha=50%)],
)

#block(breakable: false)[
*3\-영상 블록 비교 (`--comp`)*
#table(
  columns: (1.2fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [`av --comp o a b`], [3패널 원본 \| img2 \| img3 (diff 자동 비활성)],
  [#key("I")],         [정보 창: img2·img3 각각의 원본 대비 PSNR 2줄],
  [#key("Ctrl+B")],    [worst 블록 박스 토글 (\#1 빨강\~노랑; 마젠타 P = 피크 픽셀 스파이크 픽)],
  [#key("G")],         [전체 블록 그리드 바운더리 토글 (기본 OFF)],
  [#key("Ctrl+W")],    [승패 맵: 블록별 우열 색 표시 (파랑=img2·주황=img3 우세, \|Δ\|>1dB)],
  [#key("Ctrl+G")],    [블록 PSNR 히트맵 (노랑→빨강=나쁨, 50dB↑ 투명)],
  [#key("Ctrl+D")],    [worst 블록 리스트 창 — 행 클릭 시 3패널 점프],
  [#key("S")],         [img2 ↔ img3 패널 위치 스왑 (통계도 추종)],
  [#key("Tab")],       [worst 블록 순회 — *마우스가 있는 패널*의 블록만 (원본/밖=양쪽 병합, 심각도순). 3패널 자동 센터+줌, #key("Shift+Tab") 역방향],
  [#key("[") / #key("]")],  [img2(중간)의 worst 블록만 이전/다음],
  [#key("{") / #key("}")],  [img3(오른쪽)의 worst 블록만 이전/다음 (Shift+\[ \])],
  [#key(",")],         [3\-way 블링크: 원본→img2→img3 한 패널 순환 (#key("<")/#key(">") 간격)],
  [#key(";") / #key("A")], [원본 dir 다음/이전 영상 — img2·img3 같은 파일명 자동 미러],
  [박스 위 hover],      [풍선말(블록 PSNR/MSE + 상대 알고리즘 같은 블록 PSNR 병기·채널별 MSE·최악픽셀 orig/this/Δ). 반대쪽 비교 패널에도 그린 echo 박스 + *자기 통계*의 동일 형식 상세 풍선말 (원본 패널은 박스만)],
)
시작 시 stdout으로 `[comp]` PSNR·worst 블록 요약 출력 (CI/스크립트 검증용).
]

*채널 선택*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Shift+R")], [Red 채널만],
  [#key("Shift+G")], [Green 채널만],
  [#key("Shift+B")], [Blue 채널만],
  [#key("Shift+Y")], [Rec.709 루마(Y\u{2032}) 그레이뷰 토글],
  [#key("Shift+C")], [RGB 전체 (리셋)],
)

*표시 토글*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("U")], [UI 토글],
  [#key("I")], [정보 패널 토글],
  [#key("V")], [픽셀 값 풍선말],
  [#key("Ctrl+X")], [픽셀값 형식 순환 (Dec → 0xHex → Hexh)],
  [#key("P")], [Pathfinder 토글],
  [#key("Ctrl+P")], [Schematic 모드],
)

*룩 · 블링크 · 검사*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Shift+V")],           [컬러메트리 풍선말 (xy·u\u{2032}v\u{2032}·CCT, A/B/Δ)],
  [#key(",")],                 [블링크: A/B 자동 교대 토글],
  [#key("<") / #key(">")],    [블링크 간격 감소 / 증가],
  [#key("/")],                 [결함 오버레이 (NaN/Inf/음수/>1)],
  [#key("'")],                 [LUT/CDL 룩 적용 토글],
)

#colbreak()

*분석 도구*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Ctrl+H")], [히스토그램],
  [#key("Ctrl+L")], [수평 절단면 (H\-cut)],
  [#key("Ctrl+Y")], [수직 절단면 (V\-cut)],
  [#key("Ctrl+S")], [통계 (Statistics)],
  [#key("Ctrl+E")], [ROI 영역 선택],
  [#key("Ctrl+T")], [Scatter Plot],
  [#key("M")],      [Crosshair 토글],
  [#key("Shift+A")], [Slideshow 자동 재생],
  [#key("Shift+↑") / #key("Shift+↓")], [Slideshow 간격 조절],
)

*오버레이*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("O")], [오버레이 토글],
)

*이미지 시퀀스 (디렉토리 네비게이션)*
#table(
  columns: (1.3fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key(";")],                 [다음 이미지 (활성 패널)],
  [#key("a")],                 [이전 이미지 (활성 패널)],
  [#key("Tab")],               [활성 패널 전환 (A \u{2194} B) \u{2014} 테두리 굵게 강조],
  [#key("Alt") + 휠],          [네비게이션 (커서 아래 패널)],
  [#key("N") / #key("Shift+N")], [다음/이전 (레거시)],
)

로딩 직후 파일명이 상단 중앙에 1.5초 토스트로 표시되며, 끝에 도달하면 처음으로 순환합니다. A/B 패널은 서로 다른 디렉토리를 각각 독립적으로 탐색합니다.

*파일*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Shift+Ctrl+O")], [파일 열기],
  [#key("Shift+Ctrl+S")], [스크린샷 저장],
  [#key("Q")],             [종료],
)

*클립보드 복사 (2단계 키)*
#table(
  columns: (1.3fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Ctrl/Cmd+C") \u{2192} #key("1")], [Image A를 PNG로 복사],
  [#key("Ctrl/Cmd+C") \u{2192} #key("2")], [Image B를 PNG로 복사],
  [#key("Ctrl/Cmd+C") \u{2192} #key("3")], [Diff 이미지를 PNG로 복사],
  [#key("Esc") / 5초 타임아웃],              [복사 모드 취소],
)

*기타*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("W")],             [윈도우 모드 토글 (타이틀바)],
  [#key("B")],             [패널 테두리 토글 (`av.ini` 영속화)],
  [#key("S")],             [줌/팬 동기화 토글],
  [#key("Tab")],           [패널 전환],
  [#key("Shift+Space")],   [A/B 이미지 스왑],
  [#key("R")],             [90° 회전],
  [#key("Ctrl+R")],        [역방향 회전],
  [#key("Ctrl+Shift+H")],  [핫키 도움말 토글],
)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 3: 마우스
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("마우스")

#table(
  columns: (1.2fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 2pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [좌클릭 드래그],       [팬 (이동)],
  [우클릭 드래그],       [영역 줌 (Drag\-to\-Zoom)],
  [스크롤 휠],           [줌 인/아웃],
  [ROI 모드 + 드래그],   [영역 선택 (통계/분석)],
  [#key("Ctrl") 홀드 (Magnifier ON)], [마우스를 이미지 경계 내로 제한],
)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 4a: Magnifier
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("Magnifier (돋보기)")

#table(
  columns: (1.2fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 2pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Ctrl+M")],         [Magnifier 토글 (av.ini 영속화)],
  [커서 호버],               [16×16 영역 확대 \u{2014} *보이는 모든 패널에 동시 표시* (A\|B, diff 시 +Δ, `--comp` 는 orig\|img2\|img3). 각 확대경 좌상단에 패널 이름표],
  [줌 ≥ 32×],               [자동 숨김 (픽셀이 충분히 큼)],
  [#key("Ctrl") 홀드],      [마우스를 이미지 경계로 제한],
)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 4b: 지원 포맷
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("지원 포맷")

#table(
  columns: (1fr, 2.5fr),
  stroke: none,
  inset: (x: 3pt, y: 2pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [*PNG*],         [8/16\-bit, 알파 지원],
  [*JPEG*],        [표준 손실 압축, EXIF 방향 자동 적용],
  [*BMP*],         [비트맵 (Windows)],
  [*TGA*],         [Targa, 알파 채널 지원],
  [*HDR*],         [Radiance RGBE (float)],
  [*PNM P5/P6*],   [PGM/PPM Binary (자체 파서, 16\-bit 원본값 보존)],
  [*PNM P2/P3*],   [PGM/PPM ASCII (자체 파서, 원본값 보존)],
)

#v(0.5em)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 5: 픽셀값 표시
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("픽셀값 표시 우선순위")

+ PPM 원본값 (`pixels_orig` 존재 시): 0\~maxval 범위
+ HDR float: `%.2f` 형식 (hex 변환 대상 아님)
+ LDR uint8: 0\~255

줌 32× 이상에서 자동 그리드 + 값 표시. #key("V")로 풍선말 토글.

#key("Ctrl+X")로 정수 픽셀값 표시 형식을 순환 전환:
#table(
  columns: (1fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 2pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [Decimal], [`128` (기본)],
  [Hex 0x],  [`0x80` (C\-style)],
  [Hex h],   [`80h` (Intel/ASM\-style)],
)
설정은 `av.ini`에 `pixel_format=0|1|2`로 영속화.

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 6: 헤드리스 CLI (CI / 스크립트)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#section("헤드리스 CLI (CI / 스크립트)")

창·GL·SDL 없이 실행 후 즉시 종료 \u{2014} SSH·CI 파이프라인용. stdout은 순수 CSV, 요약은 stderr로 분리.

*지표 \u{2014} #raw("--metrics")*
```
av --metrics A B                        # CSV 1행 (A 대비 B)
av --pair --metrics dirA/f001.png dirB  # 시퀀스 전체(프레임당 1행)
av --metrics A B --format json|junit    # 출력 포맷
```
16컬럼: `file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,` `ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr`
(`psnr_y`=루마, `psnr_cb`/`cr`=크로마 PSNR, `msigned`=밝기 편향. 동일영상 → `inf`)

*CI 게이트* (위반 시 exit `10`)
#table(
  columns: (1.3fr, 2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [`--fail-psnr <dB>`],   [PSNR \< dB → FAIL],
  [`--warn-psnr <dB>`],   [PSNR \< dB → WARN],
  [`--fail-ssim <v>`],    [SSIM \< v → FAIL],
  [`--fail-flip <v>`],    [FLIP > v → FAIL],
  [`--fail-maxerr <v>`],  [최대오차 > v → FAIL],
)

*컬러메트리 \u{2014} #raw("--probe")*
```
av --probe X,Y A [B]
```
픽셀 (X,Y)의 CIE 값 CSV: `which,X,Y,Z,x,y,uprime,` `vprime,CCT,Duv,Lstar`. B 지정 시 `A`·`B`·`delta`(ΔCCT·Δu\u{2032}v\u{2032}·ΔE76) 행. 예: 흰색 → x\u{2009}0.3127 y\u{2009}0.3290 CCT\~6504K.

*균일도 / 무라 \u{2014} #raw("--uniformity")*
```
av --uniformity A [B]
av --uniformity A B --fail-uniformity 90 --fail-semu 1.5
```
11컬럼: `Lmean,uni9_pct,uni13_pct,uni25_pct,` `nonuni_cv_pct,duv_max,semu`. ICDM 9/13/25점 균일도%(100·Lmin/Lmax, 高=우수), CV 비균일도%(低=우수), 색편차 Δu\u{2032}v\u{2032}, SEMU 무라지수(프록시). 2장/`--pair` → `A`·`B`·`B-A`(Δ) 행. 게이트 `--fail-uniformity <pct>`·`--fail-semu <v>` → exit `10`.

*배치 \u{2014} #raw("--batch")*
```
av --batch manifest.txt          # 파일
cat pairs.tsv | av --batch -      # stdin
```
임의 A,B 쌍(같은 파일명 제약 없음). 매니페스트: 줄당 `A<TAB>B[<TAB>label]`(탭 구분), `#` 주석·빈 줄 무시. `--format`·`--fail-*` 게이트 그대로 적용.

*diff 내보내기 \u{2014} #raw("--diff-out")*
```
av --diff-out d.png --diff-mode signed [--amplify 8] [--sbs] A B
```
GUI 없이 diff 시각화를 PNG로 저장. `--sbs` = A\|Δ\|B 합성.

*결함 검사 \u{2014} #raw("--validate")*
```
av --validate img.hdr            # NaN/Inf 있으면 exit 8
```
float/HDR 이미지의 NaN·Inf·음수·>1 픽셀을 스캔(CSV).

*종료 코드*: `0` 성공 · `3` 인자 오류 · `4` 디코드 실패 · `5` 크기/포맷 불일치 · `6`/`7` diff\-out 계산/쓰기 실패 · `8` validate NaN/Inf · `10` CI 게이트 FAIL

] // end columns
