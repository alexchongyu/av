// ─────────────────────────────────────────────────────
// av — Cheat Sheet
// ─────────────────────────────────────────────────────

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
  font: ("CMU Serif", "Noto Sans CJK KR"),
  size: 8.5pt,
  lang: "ko",
)

#set par(
  justify: false,
  leading: 0.55em,
  spacing: 0.6em,
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
  #text(size: 8pt, fill: luma(120))[v0.14 \u{2014} 2026\-03\-17]
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
  [`-p <N>`],               [팬 이동 단위 (픽셀)],
  [`-bc <A> <B> <D>`],      [패널 테두리 색상 (hex)],
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
  [#key("Ctrl+3")], [Absolute diff],
  [#key("Ctrl+4")], [Relative diff],
  [#key("Ctrl+5")], [False color],
  [#key("Ctrl+6")], [SSIM],
  [#key("Ctrl+7")], [Tolerance diff 토글],
  [#key("Shift+]") / #key("Shift+[")], [Threshold +1 / \-1],
  [#key("Ctrl+\\")], [Threshold 리셋],
)

*Diff 증폭*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("[") / #key("]")], [증폭 \-0.5 / +0.5],
  [#key("\\")],            [증폭 리셋 (1.0)],
)

*채널 선택*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("Shift+R")], [Red 채널만],
  [#key("Shift+G")], [Green 채널만],
  [#key("Shift+B")], [Blue 채널만],
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
  [#key("A")],      [Slideshow 자동 재생],
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

*이미지 시퀀스*
#table(
  columns: (1fr, 2.2fr),
  stroke: none,
  inset: (x: 3pt, y: 1.5pt),
  fill: (_, y) => if calc.odd(y) { luma(248) } else { white },
  [#key("N")],         [다음 이미지],
  [#key("Shift+N")],   [이전 이미지],
)

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
)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SECTION 4: 지원 포맷
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
  [*PNM P5/P6*],   [PGM/PPM Binary (stb\_image)],
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

] // end columns
