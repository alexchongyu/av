// ─────────────────────────────────────────────────────────────
// av User's Manual — 공용 헬퍼 + cetz 그림
// 마스터(av_users_manual.typ)와 모든 챕터가 이 파일을 import 한다.
// 스타일(#set/#show)은 마스터가 소유한다. 여기에는 #let 만 둔다.
// ─────────────────────────────────────────────────────────────

#import "@preview/cetz:0.4.2": canvas, draw

// ── 팔레트 ────────────────────────────────────────────────────
#let cInk    = luma(35)
#let cSoft   = luma(110)
#let cLine   = luma(150)
#let cBg     = luma(246)
#let cOrig   = rgb("#475569")   // 원본 패널
#let cImg2   = rgb("#2563eb")   // img2 / A
#let cImg3   = rgb("#ea580c")   // img3 / B
#let cWorst  = rgb("#dc2626")   // worst #1
#let cWarm   = rgb("#eab308")   // worst 말단(노랑)
#let cSpike  = rgb("#c026d3")   // 스파이크 픽(마젠타)
#let cEcho   = rgb("#059669")   // hover echo(초록)
#let cOk     = rgb("#16a34a")
#let cBad    = rgb("#dc2626")

// ── 키캡 ──────────────────────────────────────────────────────
#let key(k) = box(
  fill: luma(238),
  inset: (x: 3.5pt, y: 1.5pt),
  outset: (y: 1.5pt),
  radius: 2.5pt,
  stroke: 0.4pt + luma(170),
  text(size: 0.86em, font: "CMU Typewriter Text", fill: luma(20), k),
)

// ── 콜아웃 ────────────────────────────────────────────────────
#let _callout(mark, tone, body) = block(
  width: 100%,
  fill: tone.lighten(90%),
  stroke: (left: 2.2pt + tone, rest: 0.4pt + tone.lighten(55%)),
  inset: (x: 0.8em, y: 0.6em),
  radius: (right: 3pt),
  spacing: 1.1em,
  {
    text(weight: "bold", fill: tone.darken(12%), size: 0.92em, mark)
    h(0.45em)
    body
  },
)

#let note(body) = _callout("참고", rgb("#2563eb"), body)
#let tip(body)  = _callout("팁",   rgb("#0f9d58"), body)
#let warn(body) = _callout("주의", rgb("#ea580c"), body)

// ── 예시 블록 ─────────────────────────────────────────────────
#let ex(title, body) = block(
  width: 100%,
  fill: luma(250),
  stroke: (paint: luma(190), thickness: 0.5pt, dash: "densely-dotted"),
  inset: (x: 0.8em, y: 0.65em),
  radius: 3pt,
  spacing: 1.15em,
  {
    text(weight: "bold", size: 0.9em, fill: luma(55))[예시 \u{2014} #title]
    v(0.35em, weak: true)
    body
  },
)

// ─────────────────────────────────────────────────────────────
// 그림 공용 유틸 (cetz draw 요소를 반환)
// ─────────────────────────────────────────────────────────────

#let _t(s, sz: 7.5pt, fill: cInk, weight: "regular") = text(
  size: sz, fill: fill, weight: weight, font: ("CMU Serif", "Noto Sans KR"), s,
)

// 라벨 붙은 상자
#let _box(a, b, label, fill: white, stroke: 0.6pt + cLine, sz: 7.5pt, tc: cInk, w: "regular") = {
  draw.rect(a, b, fill: fill, stroke: stroke)
  draw.content(((a.at(0) + b.at(0)) / 2, (a.at(1) + b.at(1)) / 2), _t(label, sz: sz, fill: tc, weight: w))
}

// 화살표
#let _ar(a, b, stroke: 0.7pt + cSoft) = draw.line(
  a, b, stroke: stroke, mark: (end: ">", fill: cSoft, scale: 0.42),
)

// 원 번호
#let _num(pos, n, fill: cInk) = {
  draw.circle(pos, radius: 0.17, fill: fill, stroke: none)
  draw.content(pos, _t([#n], sz: 6.2pt, fill: white, weight: "bold"))
}

// 색 스톱 보간
#let _stops(cs, t) = {
  let n = cs.len() - 1
  let s = calc.clamp(t, 0.0, 1.0) * n
  let i = calc.min(calc.floor(s), n - 1)
  let u = s - i
  color.mix((cs.at(i), (1.0 - u) * 100%), (cs.at(i + 1), u * 100%))
}

// 그라데이션 바
#let _bar(x0, y0, w, h, cs, n: 60) = {
  for i in range(n) {
    draw.rect(
      (x0 + w * i / n, y0), (x0 + w * (i + 1) / n, y0 + h),
      fill: _stops(cs, i / (n - 1)), stroke: none,
    )
  }
  draw.rect((x0, y0), (x0 + w, y0 + h), stroke: 0.4pt + luma(90))
}

// 풍선말(작은 다중 행 상자)
#let _balloon(pos, body, anchor: "north-west") = draw.content(
  pos,
  box(
    fill: white, stroke: 0.45pt + luma(120), inset: 3pt, radius: 2pt,
    par(leading: 0.42em, text(size: 5.4pt, font: ("CMU Serif", "Noto Sans KR"), body)),
  ),
  anchor: anchor,
)

// 마우스 커서 표식
#let _cursor(pos) = {
  let x = pos.at(0)
  let y = pos.at(1)
  draw.line((x, y), (x, y - 0.42), (x + 0.11, y - 0.30), (x + 0.25, y - 0.55),
            (x + 0.33, y - 0.50), (x + 0.19, y - 0.26), (x + 0.36, y - 0.24),
            close: true, fill: black, stroke: 0.4pt + white)
}

#let _fig(cap, body) = figure(
  kind: image, supplement: [그림], caption: cap, body,
)

// ─────────────────────────────────────────────────────────────
// 그림 1 — 앱 화면 구성
// ─────────────────────────────────────────────────────────────
#let fig_ui_layout() = _fig(
  [av 창의 구성 요소. 활성 패널은 테두리가 굵게 강조된다.],
  canvas(length: 1cm, {
    import draw: *

    // 창 외곽
    rect((0.1, 0.2), (11.2, 7.9), fill: luma(252), stroke: 0.8pt + cInk)
    // 메뉴바
    _box((0.1, 7.3), (11.2, 7.9), "", fill: luma(232), stroke: 0.5pt + cLine)
    content((0.35, 7.6), _t("File   View   Diff   Help", sz: 6.5pt), anchor: "west")
    // 패널 A (활성)
    rect((0.35, 1.15), (5.55, 7.05), fill: luma(240), stroke: 1.6pt + rgb("#d946ef"))
    content((2.95, 6.75), _t("Image A", sz: 7pt, fill: cSoft))
    // 패널 B
    rect((5.75, 1.15), (10.95, 7.05), fill: luma(240), stroke: 0.8pt + rgb("#ca8a04"))
    content((8.35, 6.75), _t("Image B", sz: 7pt, fill: cSoft))
    // 상태바
    rect((0.1, 0.2), (11.2, 0.95), fill: luma(228), stroke: 0.5pt + cLine)
    content((0.85, 0.57), _t("A: frame_012.png  1920x1080   zoom 100%   PSNR 42.13 dB", sz: 6pt), anchor: "west")
    // 정보 창
    rect((7.6, 3.9), (10.75, 6.5), fill: white, stroke: 0.55pt + cSoft)
    rect((7.6, 6.15), (10.75, 6.5), fill: luma(225), stroke: 0.55pt + cSoft)
    content((8.35, 6.32), _t("Image Info", sz: 5.8pt, weight: "bold"), anchor: "west")
    for (i, s) in (
      "file  frame_012.png", "size  1920 x 1080", "fmt   PNG 8-bit RGBA",
      "PSNR  42.13 dB", "SSIM  0.9931",
    ).enumerate() {
      content((7.75, 5.85 - i * 0.33), _t(s, sz: 5.4pt, fill: luma(60)), anchor: "west")
    }
    // 픽셀 풍선말
    _balloon((2.0, 4.4), [(812, 431)\ R 128  G 96  B 240\ A 255])
    _cursor((1.95, 4.55))
    // 토스트
    rect((4.3, 6.55), (6.9, 7.05), fill: rgb("#00000022"), stroke: 0.4pt + cSoft)
    content((5.6, 6.8), _t("frame_012.png", sz: 5.8pt))

    // 콜아웃 번호
    _num((0.55, 7.6), 1)
    _num((0.75, 6.75), 2)
    _num((6.05, 6.25), 3)
    _num((10.5, 6.32), 4)
    _num((1.75, 4.9), 5)
    _num((4.08, 6.8), 6)
    _num((0.55, 0.57), 7)

    // 범례
    for (i, s) in (
      ([1], "메뉴바 \u{2014} 모든 기능의 목록과 단축키"),
      ([2], "이미지 패널 A (활성: 굵은 테두리)"),
      ([3], "이미지 패널 B"),
      ([4], "정보 창 \u{2014} I 키"),
      ([5], "픽셀 값 풍선말 \u{2014} V 키"),
      ([6], "파일명 토스트 (1.5초)"),
      ([7], "상태바 \u{2014} 파일·크기·줌·지표"),
    ).enumerate() {
      let y = 7.5 - i * 0.62
      _num((11.75, y), s.at(0))
      content((12.05, y), _t(s.at(1), sz: 6.6pt), anchor: "west")
    }
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 2 — 패널 레이아웃 모드
// ─────────────────────────────────────────────────────────────
#let fig_panel_modes() = _fig(
  [입력 이미지 개수와 옵션에 따른 패널 레이아웃.],
  canvas(length: 1cm, {
    import draw: *
    let W = 3.55
    let H = 2.5
    let y0 = 1.35
    let modes = (
      ("av a.png", ("A",), (cImg2,), "1장 \u{2014} 단순 뷰어"),
      ("av a.png b.png", ("A", "B"), (cImg2, cImg3), "2장 \u{2014} 나란히 비교"),
      ("av a.png b.png -d", ("A", "B", "\u{0394}"), (cImg2, cImg3, cWorst), "2장 + diff \u{2014} 3패널"),
      ("av --comp o a b", ("orig", "img2", "img3"), (cOrig, cImg2, cImg3), "3장 \u{2014} 블록 비교"),
    )
    for (m, mode) in modes.enumerate() {
      let x0 = 0.15 + m * (W + 0.42)
      rect((x0, y0), (x0 + W, y0 + H), fill: luma(250), stroke: 0.7pt + cInk)
      rect((x0, y0 + H - 0.28), (x0 + W, y0 + H), fill: luma(230), stroke: 0.5pt + cLine)
      let names = mode.at(1)
      let cols = mode.at(2)
      let n = names.len()
      let pw = (W - 0.24 - 0.1 * (n - 1)) / n
      for (i, nm) in names.enumerate() {
        let px = x0 + 0.12 + i * (pw + 0.1)
        rect((px, y0 + 0.35), (px + pw, y0 + H - 0.42), fill: luma(238), stroke: 0.9pt + cols.at(i))
        content((px + pw / 2, y0 + (H - 0.07) / 2), _t(nm, sz: 6.6pt, fill: cols.at(i), weight: "bold"))
      }
      content((x0 + W / 2, y0 - 0.28), _t(raw(mode.at(0)), sz: 6.2pt))
      content((x0 + W / 2, y0 - 0.72), _t(mode.at(3), sz: 7pt, weight: "bold"))
    }
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 3 — 줌·팬 좌표 모델
// ─────────────────────────────────────────────────────────────
#let fig_zoom_pan_model() = _fig(
  [이미지 좌표에서 화면 좌표로의 변환. 줌은 배율을, 팬은 평행이동을 담당한다.],
  canvas(length: 1cm, {
    import draw: *
    // 이미지 공간
    content((3.3, 5.5), _t("이미지 좌표 (픽셀 격자)", sz: 8.4pt, weight: "bold"))
    grid((0.6, 1.5), (6.0, 5.1), step: 0.45, stroke: 0.25pt + luma(200))
    rect((0.6, 1.5), (6.0, 5.1), stroke: 0.7pt + cInk)
    rect((3.30, 3.30), (3.75, 3.75), fill: cWorst.lighten(35%), stroke: 0.6pt + cWorst)
    content((3.52, 2.95), _t("(x, y)", sz: 6.4pt, fill: cWorst))
    content((0.6, 1.2), _t("(0,0)", sz: 6.2pt, fill: cSoft), anchor: "west")
    content((6.0, 1.2), _t("img_size", sz: 6.2pt, fill: cSoft), anchor: "east")

    // 변환 화살표
    _ar((6.35, 3.4), (8.05, 3.4), stroke: 1.0pt + cImg2)
    content((7.2, 3.95), _t("\u{00D7} zoom", sz: 7.4pt, fill: cImg2))
    content((7.2, 2.95), _t("+ pan", sz: 7.4pt, fill: cImg2))

    // 화면(뷰포트)
    content((12.2, 5.5), _t("화면 뷰포트 (패널 영역)", sz: 8.4pt, weight: "bold"))
    rect((8.4, 1.5), (15.9, 5.1), fill: luma(247), stroke: 0.7pt + cInk)
    grid((9.3, 1.95), (14.7, 4.65), step: 0.9, stroke: 0.25pt + luma(205))
    rect((9.3, 1.95), (14.7, 4.65), stroke: 0.5pt + cSoft)
    rect((13.80, 3.75), (14.70, 4.65), fill: cWorst.lighten(35%), stroke: 0.6pt + cWorst)
    content((12.0, 1.2), _t("zoom = 2\u{00D7} \u{2014} 이미지 픽셀 한 개가 화면에서 2\u{00D7}2 점", sz: 6.4pt, fill: cSoft))
    content((15.75, 4.9), _t("pan 으로 이 창이 이동", sz: 6pt, fill: cSoft), anchor: "east")

    // 수식
    rect((0.6, 0.15), (15.9, 0.85), fill: luma(249), stroke: 0.4pt + cLine)
    content((8.25, 0.5), _t(raw("screen = panel_pos + (img_px + pan - img_size/2) * zoom + view_size/2"), sz: 7pt))
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 4 — 픽셀 값 판독 우선순위
// ─────────────────────────────────────────────────────────────
#let fig_pixel_priority() = _fig(
  [픽셀 값 판독 우선순위와 표시 형식 변환. 원본 값 보존이 최우선이다.],
  canvas(length: 1cm, {
    import draw: *
    let dia(pos, label) = {
      let x = pos.at(0)
      let y = pos.at(1)
      line((x - 1.75, y), (x, y + 0.78), (x + 1.75, y), (x, y - 0.78), close: true,
           fill: rgb("#fef3c7"), stroke: 0.6pt + rgb("#b45309"))
      content((x, y), _t(label, sz: 6.0pt))
    }
    _box((0.2, 4.45), (3.0, 5.25), "디코드된 픽셀 버퍼", fill: luma(240), sz: 7pt)
    _ar((3.0, 4.85), (3.85, 4.85))
    dia((5.6, 4.85), [PNM 원본값\ (pixels_orig) 존재?])
    _ar((7.35, 4.85), (8.2, 4.85))
    _box((8.2, 4.45), (12.4, 5.25), "원본 정수값 그대로 (0 \u{2013} maxval)", fill: cOk.lighten(85%), stroke: 0.6pt + cOk, sz: 7pt)
    content((7.65, 5.05), _t("예", sz: 6pt, fill: cOk, weight: "bold"))

    _ar((5.6, 4.07), (5.6, 3.41))
    content((5.85, 3.75), _t("아니오", sz: 6pt, fill: cSoft), anchor: "west")
    dia((5.6, 2.63), [부동소수(HDR/float)?])
    _ar((7.35, 2.63), (8.2, 2.63))
    _box((8.2, 2.23), (12.4, 3.03), "float 그대로 \u{2014} %.2f 형식", fill: cImg2.lighten(88%), stroke: 0.6pt + cImg2, sz: 7pt)
    content((7.65, 2.83), _t("예", sz: 6pt, fill: cImg2, weight: "bold"))

    _ar((5.6, 1.85), (5.6, 1.25))
    content((5.85, 1.6), _t("아니오", sz: 6pt, fill: cSoft), anchor: "west")
    _box((3.6, 0.45), (7.6, 1.25), "LDR uint8 (0 \u{2013} 255)", fill: luma(240), sz: 7pt)

    _ar((7.6, 0.85), (8.6, 0.85))
    rect((8.6, 0.25), (13.6, 1.45), fill: luma(250), stroke: 0.5pt + cLine)
    content((11.1, 1.22), _t([표시 형식 순환 \u{2014} #raw("Ctrl+X")], sz: 6.6pt, weight: "bold"))
    for (i, s) in ((raw("128"), "Decimal"), (raw("0x80"), "Hex 0x"), (raw("80h"), "Hex h")).enumerate() {
      content((9.15 + i * 1.65, 0.78), _t(s.at(0), sz: 6.6pt))
      content((9.15 + i * 1.65, 0.48), _t(s.at(1), sz: 5.6pt, fill: cSoft))
    }
    content((10.0, 0.78), _t("\u{2192}", sz: 7pt, fill: cSoft))
    content((11.65, 0.78), _t("\u{2192}", sz: 7pt, fill: cSoft))
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 5 — Diff 파이프라인
// ─────────────────────────────────────────────────────────────
#let fig_diff_pipeline() = _fig(
  [두 영상이 Diff 패널과 지표로 흘러가는 경로. 지표는 표시 룩의 영향을 받지 않는다.],
  canvas(length: 1cm, {
    import draw: *
    _box((0.15, 4.3), (2.05, 5.1), "Image A", fill: cImg2.lighten(88%), stroke: 0.7pt + cImg2, sz: 7.2pt)
    _box((0.15, 3.1), (2.05, 3.9), "Image B", fill: cImg3.lighten(88%), stroke: 0.7pt + cImg3, sz: 7.2pt)
    _ar((2.05, 4.7), (2.9, 4.35))
    _ar((2.05, 3.5), (2.9, 3.85))
    _box((2.9, 3.7), (5.0, 4.5), "크기·포맷 검사", fill: luma(242), sz: 7pt)
    _ar((5.0, 4.1), (5.9, 4.1))
    _box((5.9, 3.7), (7.7, 4.5), "Diff 엔진", fill: luma(235), sz: 7.2pt, w: "bold")

    // 모드 목록
    rect((8.5, 2.35), (12.9, 5.85), fill: luma(250), stroke: 0.5pt + cLine)
    content((10.7, 5.62), _t("모드 선택", sz: 6.8pt, weight: "bold"))
    for (i, s) in (
      "Ctrl+0  FLIP (지각 오차)", "Ctrl+1  Signed (부호차)",
      "Ctrl+2  Alpha Blend", "Ctrl+3  Absolute",
      "Ctrl+4  Relative", "Ctrl+5  Enhance",
      "Ctrl+6  False Color", "Ctrl+7  SSIM",
      "Ctrl+9  Highlight", "Ctrl+8  Tolerance 토글",
    ).enumerate() {
      content((8.7, 5.32 - i * 0.32), _t(raw(s), sz: 5.8pt), anchor: "west")
    }
    _ar((7.7, 4.25), (8.5, 4.25))

    // 후처리 → 표시
    _ar((10.7, 2.35), (10.7, 1.85))
    _box((9.0, 1.05), (12.4, 1.85), [증폭 #raw("[ ]") · 임계 #raw("Shift+[ ]")], fill: luma(242), sz: 6.6pt)
    _ar((9.0, 1.45), (8.1, 1.45))
    _box((5.5, 1.05), (8.1, 1.85), "색맵 적용", fill: luma(242), sz: 7pt)
    _ar((5.5, 1.45), (4.6, 1.45))
    _box((2.2, 1.05), (4.6, 1.85), "Diff 패널 표시", fill: cWorst.lighten(88%), stroke: 0.7pt + cWorst, sz: 7pt)

    // 지표 분기
    line((1.1, 3.1), (1.1, 2.95), stroke: (paint: cOk, thickness: 0.6pt, dash: "dashed"))
    _ar((1.1, 2.95), (1.1, 2.78), stroke: 0.6pt + cOk)
    _box((1.0, 1.95), (4.3, 2.75), "지표: PSNR / SSIM / FLIP", fill: cOk.lighten(88%), stroke: 0.6pt + cOk, sz: 6.6pt)
    content((2.9, 2.95), _t("원시 픽셀 \u{2014} 룩 적용 전", sz: 5.8pt, fill: cOk.darken(10%)), anchor: "west")
    _ar((4.3, 2.35), (5.0, 2.35), stroke: 0.6pt + cSoft)
    content((5.15, 2.35), _t([정보 창 · 상태바 · #raw("--metrics") CSV], sz: 6.2pt), anchor: "west")
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 6 — Diff 색맵
// ─────────────────────────────────────────────────────────────
#let fig_diff_colormaps() = _fig(
  [주요 Diff 모드의 색 해석. 같은 색이라도 모드마다 의미가 다르다.],
  canvas(length: 1cm, {
    import draw: *
    let rowy = (4.5, 2.7, 0.9)
    // Signed
    content((0.1, rowy.at(0) + 0.95), _t([Signed \u{2014} #raw("Ctrl+1")], sz: 7.4pt, weight: "bold"), anchor: "west")
    _bar(0.1, rowy.at(0), 11.4, 0.62, (rgb("#1d4ed8"), white, rgb("#dc2626")))
    for (i, s) in (("A < B (B가 밝음)", 0.0), ("A = B", 0.5), ("A > B (A가 밝음)", 1.0)).enumerate() {
      content((0.1 + 11.4 * s.at(1), rowy.at(0) - 0.28), _t(s.at(0), sz: 6pt, fill: cSoft))
    }
    content((11.75, rowy.at(0) + 0.31), _t("부호 있는 차이 \u{2014} 밝기 편향 방향", sz: 6.4pt), anchor: "west")

    // False color
    content((0.1, rowy.at(1) + 0.95), _t([False Color \u{2014} #raw("Ctrl+6")], sz: 7.4pt, weight: "bold"), anchor: "west")
    _bar(0.1, rowy.at(1), 11.4, 0.62,
      (rgb("#000428"), rgb("#0891b2"), rgb("#16a34a"), rgb("#facc15"), rgb("#dc2626")))
    content((0.1, rowy.at(1) - 0.28), _t("차이 0", sz: 6pt, fill: cSoft))
    content((11.5, rowy.at(1) - 0.28), _t("차이 최대", sz: 6pt, fill: cSoft))
    content((11.75, rowy.at(1) + 0.31), _t("크기(절대값) 스케일", sz: 6.4pt), anchor: "west")

    // FLIP magma
    content((0.1, rowy.at(2) + 0.95), _t([FLIP \u{2014} #raw("Ctrl+0")], sz: 7.4pt, weight: "bold"), anchor: "west")
    _bar(0.1, rowy.at(2), 11.4, 0.62,
      (rgb("#000004"), rgb("#51127c"), rgb("#b73779"), rgb("#fc8961"), rgb("#fcfdbf")))
    content((0.35, rowy.at(2) - 0.28), _t("지각 오차 0", sz: 6pt, fill: cSoft))
    content((11.3, rowy.at(2) - 0.28), _t("지각 오차 1", sz: 6pt, fill: cSoft))
    content((11.75, rowy.at(2) + 0.31), _t("사람 눈 기준 체감 오차(magma)", sz: 6.4pt), anchor: "west")
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 7 — comp 3패널 화면
// ─────────────────────────────────────────────────────────────
#let fig_comp_layout() = _fig(
  [#raw("--comp") 3패널 화면. 마우스를 img2의 블록에 올리면 나머지 두 패널에 초록 echo 박스가 뜨고, 비교 패널(img3)에는 자기 통계로 계산한 같은 형식의 풍선말이 함께 나타난다.],
  canvas(length: 1cm, {
    import draw: *
    let names = ("원본 (orig)", "img2 (중간)", "img3 (오른쪽)")
    let cols = (cOrig, cImg2, cImg3)
    let W = 4.9
    for i in range(3) {
      let x0 = 0.15 + i * (W + 0.28)
      rect((x0, 1.35), (x0 + W, 6.5), fill: luma(243), stroke: 1.0pt + cols.at(i))
      rect((x0, 6.5), (x0 + W, 6.95), fill: cols.at(i).lighten(80%), stroke: 0.6pt + cols.at(i))
      content((x0 + W / 2, 6.72), _t(names.at(i), sz: 6.8pt, fill: cols.at(i).darken(15%), weight: "bold"))
    }
    // 블록 그리드 흔적 (img2)
    let x2 = 0.15 + (W + 0.28)
    let x3 = 0.15 + 2 * (W + 0.28)
    grid((x2 + 0.2, 1.6), (x2 + 4.7, 6.3), step: 0.45, stroke: 0.18pt + luma(215))

    // worst 블록들 (img2)
    let blks = (
      (0.65, 4.95, cWorst, "#1"), (2.45, 3.60, rgb("#f97316"), "#2"),
      (3.80, 5.40, cWarm, "#5"), (1.55, 2.25, cWarm, "#7"),
      (3.35, 2.70, cSpike, "#4P"),
    )
    for b in blks {
      rect((x2 + b.at(0), b.at(1)), (x2 + b.at(0) + 0.45, b.at(1) + 0.45), stroke: 1.1pt + b.at(2))
      content((x2 + b.at(0) + 0.22, b.at(1) + 0.62), _t(b.at(3), sz: 5.2pt, fill: b.at(2), weight: "bold"))
    }
    // hover 대상 = #1
    _cursor((x2 + 0.95, 5.28))
    _balloon((x2 + 1.25, 5.35), [
      *블록 (20, 10)* \
      PSNR 22.89 dB \
      MSE 333.90 \
      img3 같은 블록 31.44 dB (d +8.55) \
      최악 픽셀 (163,84) 118 \u{2192} 40 (\u{0394} \u{2212}78)
    ])

    // echo: 원본(박스만)
    rect((0.15 + 0.65, 4.95), (0.15 + 1.10, 5.40), stroke: 1.3pt + cEcho)
    content((0.15 + 0.88, 4.72), _t("박스만", sz: 5.2pt, fill: cEcho))
    // echo: img3(박스 + 자기 통계 풍선말)
    rect((x3 + 0.65, 4.95), (x3 + 1.10, 5.40), stroke: 1.3pt + cEcho)
    _balloon((x3 + 1.20, 5.35), [
      *블록 (20, 10)* \
      PSNR 31.44 dB \
      MSE 46.68 \
      최악 픽셀 (163,84) 118 \u{2192} 96 (\u{0394} \u{2212}22)
    ])

    // 상태바 요약
    rect((0.15, 0.72), (15.35, 1.22), fill: luma(232), stroke: 0.5pt + cLine)
    content((0.35, 0.97), _t(raw("comp  P2 34.21 dB   P3 36.88 dB   W2 118  W3 402  T 1580"), sz: 6pt), anchor: "west")
    content((15.15, 0.97), _t("상태바 요약", sz: 5.8pt, fill: cSoft), anchor: "east")

    // 범례
    let leg = (
      (cWorst, "worst #1 (가장 나쁨)"), (cWarm, "worst 하위 순위"),
      (cSpike, "P = 피크 픽셀 스파이크 픽"), (cEcho, "hover echo (다른 패널)"),
    )
    for (i, l) in leg.enumerate() {
      let x = 0.3 + i * 3.9
      rect((x, 0.12), (x + 0.34, 0.44), stroke: 1.1pt + l.at(0))
      content((x + 0.5, 0.28), _t(l.at(1), sz: 6pt), anchor: "west")
    }
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 8 — worst 블록 선정 알고리즘
// ─────────────────────────────────────────────────────────────
#let fig_comp_ranking() = _fig(
  [worst 블록 선정은 지표 MSE 랭킹 75%와 피크 픽셀 오차 랭킹 25%를 섞는다. 오른쪽 두 블록은 MSE는 비슷하지만 성격이 전혀 다른 결함이다.],
  canvas(length: 1cm, {
    import draw: *
    _box((0.15, 3.9), (2.85, 4.9), "전체 블록 스캔", fill: luma(240), sz: 7pt)
    content((1.5, 3.62), _t([블록 크기 #raw("--blk") (기본 8x8)], sz: 6pt, fill: cSoft))
    _ar((2.85, 4.55), (3.75, 5.05))
    _ar((2.85, 4.25), (3.75, 3.75))
    _box((3.75, 4.65), (7.55, 5.45), "지표 MSE 내림차순", fill: cImg2.lighten(88%), stroke: 0.6pt + cImg2, sz: 7pt)
    _box((3.75, 3.35), (7.55, 4.15), "블록 내 최대 픽셀 오차 내림차순", fill: cSpike.lighten(88%), stroke: 0.6pt + cSpike, sz: 6.6pt)
    _ar((7.55, 5.05), (8.5, 4.75))
    _ar((7.55, 3.75), (8.5, 4.05))
    _box((8.5, 3.95), (11.6, 4.85), [상위 \u{2308}3N/4\u{2309} + 나머지 N/4], fill: luma(240), sz: 6.8pt)
    _ar((11.6, 4.4), (12.5, 4.4))
    _box((12.5, 3.95), (15.85, 4.85), [worst 리스트 N개\ (#raw("--num_blk"), 기본 16)], fill: cOk.lighten(88%), stroke: 0.6pt + cOk, sz: 6.6pt)
    content((10.05, 3.62), _t(raw("--num_blk 16  ->  12 mse + 4 peak"), sz: 6pt, fill: cSoft))
    content((13.9, 3.55), _t([화면 박스 · #raw("Ctrl+D") 리스트 · #raw("Tab") 순회], sz: 5.8pt, fill: cSoft))

    // 두 블록 비교 예시
    content((0.6, 2.78), _t("같은 MSE, 다른 결함 \u{2014} 왜 두 랭킹을 섞는가", sz: 7.4pt, weight: "bold"), anchor: "west")
    // (a) 고른 오차
    for i in range(8) {
      for j in range(8) {
        let v = 0.14 + 0.06 * calc.rem(i * 5 + j * 3, 4)
        rect((0.6 + i * 0.22, 0.42 + j * 0.22), (0.82 + i * 0.22, 0.64 + j * 0.22),
             fill: luma(100%).darken(v * 100%), stroke: 0.12pt + luma(210))
      }
    }
    rect((0.6, 0.42), (2.36, 2.18), stroke: 0.7pt + cImg2)
    content((1.48, 2.27), _t("(a) 전체가 조금씩 어긋남", sz: 6.2pt, fill: cImg2), anchor: "south")
    content((1.48, 0.18), _t("MSE 높음 \u{2192} MSE 랭킹이 잡음", sz: 5.8pt, fill: cSoft))

    // (b) 단일 스파이크
    for i in range(8) {
      for j in range(8) {
        rect((4.1 + i * 0.22, 0.42 + j * 0.22), (4.32 + i * 0.22, 0.64 + j * 0.22),
             fill: white, stroke: 0.12pt + luma(210))
      }
    }
    rect((4.1 + 4 * 0.22, 0.42 + 5 * 0.22), (4.32 + 4 * 0.22, 0.64 + 5 * 0.22),
         fill: rgb("#111827"), stroke: 0.3pt + cSpike)
    rect((4.1, 0.42), (5.86, 2.18), stroke: 0.7pt + cSpike)
    content((4.98, 2.27), _t("(b) 한 픽셀만 크게 튐", sz: 6.2pt, fill: cSpike), anchor: "south")
    content((4.98, 0.18), _t("MSE 낮음 \u{2192} 피크 랭킹만이 잡음", sz: 5.8pt, fill: cSoft))

    // 설명
    rect((6.6, 0.2), (15.85, 2.5), fill: luma(250), stroke: 0.45pt + cLine)
    content((6.9, 2.22), _t("(b)는 8\u{00D7}8 안에서 63픽셀이 정확하고 1픽셀만 크게 어긋난 경우다.", sz: 6.6pt), anchor: "west")
    content((6.9, 1.88), _t("MSE는 64로 나누므로 값이 묻히지만, 화면에서는 반짝이는 점 결함으로", sz: 6.6pt), anchor: "west")
    content((6.9, 1.54), _t("바로 보인다. 그래서 av는 상위 3/4을 지표 MSE로, 나머지 1/4을 블록 내", sz: 6.6pt), anchor: "west")
    content((6.9, 1.20), _t("최대 오차로 뽑고 후자를 마젠타 P 로 표시한다.", sz: 6.6pt), anchor: "west")
    content((6.9, 0.78), _t([지표 기준은 #raw("--blk-metric rgb|y|chroma") 로 바꾼다 \u{2014} 예를 들어 크로마], sz: 6.6pt), anchor: "west")
    content((6.9, 0.44), _t("결함만 추적하려면 chroma 를 쓴다.", sz: 6.6pt), anchor: "west")
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 9 — worst 블록 순회 스코프
// ─────────────────────────────────────────────────────────────
#let fig_comp_nav() = _fig(
  [#raw("Tab") 의 순회 대상은 마우스가 올라가 있는 패널로 결정된다. 대괄호 키는 마우스 위치와 무관하게 대상이 고정된다.],
  canvas(length: 1cm, {
    import draw: *
    let W = 4.9
    let names = ("원본 (orig)", "img2 (중간)", "img3 (오른쪽)")
    let cols = (cOrig, cImg2, cImg3)
    let caps = (
      "img2 + img3 병합\n(심각도 순)",
      "img2 의 worst 블록만",
      "img3 의 worst 블록만",
    )
    for i in range(3) {
      let x0 = 0.15 + i * (W + 0.28)
      rect((x0, 2.4), (x0 + W, 5.4), fill: luma(245), stroke: 1.0pt + cols.at(i))
      content((x0 + W / 2, 5.62), _t(names.at(i), sz: 7pt, fill: cols.at(i).darken(15%), weight: "bold"))
      _cursor((x0 + W / 2, 4.3))
      content((x0 + W / 2, 3.55), _t("마우스가 여기 있으면", sz: 6.2pt, fill: cSoft))
      _ar((x0 + W / 2, 2.4), (x0 + W / 2, 1.95))
      rect((x0 + 0.35, 1.05), (x0 + W - 0.35, 1.9), fill: cols.at(i).lighten(88%), stroke: 0.6pt + cols.at(i))
      content((x0 + W / 2, 1.62), _t([#raw("Tab") 순회 대상], sz: 6.4pt, weight: "bold"))
      content((x0 + W / 2, 1.28), _t(caps.at(i).replace("\n", " "), sz: 6.2pt))
    }
    content((0.15 + W / 2, 3.15), _t("(패널 밖에 있을 때도 동일)", sz: 5.8pt, fill: cSoft))

    // 고정 스코프 키
    rect((0.15, 0.1), (15.35, 0.8), fill: luma(250), stroke: 0.45pt + cLine)
    content((0.35, 0.45), _t([마우스와 무관하게 고정 \u{2014} #raw("[") #raw("]") : img2 전용 이전/다음,  #raw("Shift+[") #raw("Shift+]") (= #raw("{") #raw("}")) : img3 전용,  #raw("Shift+Tab") : 역방향.  선택하면 3패널이 동시에 센터 + 줌된다.], sz: 6.0pt), anchor: "west")
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 10 — 분석 창
// ─────────────────────────────────────────────────────────────
#let fig_analysis_windows() = _fig(
  [이미지 패널을 중심으로 열리는 분석 창들과 단축키.],
  canvas(length: 1cm, {
    import draw: *
    // 중앙
    rect((6.0, 2.85), (10.0, 4.35), fill: luma(238), stroke: 0.9pt + cInk)
    content((8.0, 3.85), _t("이미지 패널 A / B", sz: 8pt, weight: "bold"))
    content((8.0, 3.35), _t("커서 위치 · ROI 영역이 입력", sz: 6.2pt, fill: cSoft))

    let sats = (
      (0.2, 5.3, "히스토그램", "Ctrl+H", "hist"),
      (5.6, 5.9, "H\u{2013}Line Cut", "Ctrl+L", "hcut"),
      (11.0, 5.3, "V\u{2013}Line Cut", "Ctrl+Y", "vcut"),
      (0.2, 0.5, "통계", "Ctrl+S", "stat"),
      (5.6, 0.15, "ROI 통계", "Ctrl+E", "roi"),
      (11.0, 0.5, "Scatter Plot", "Ctrl+T", "scat"),
    )
    for s in sats {
      let x = s.at(0)
      let y = s.at(1)
      rect((x, y), (x + 4.4, y + 1.55), fill: white, stroke: 0.6pt + cSoft)
      rect((x, y + 1.2), (x + 4.4, y + 1.55), fill: luma(232), stroke: 0.6pt + cSoft)
      content((x + 0.15, y + 1.37), _t(s.at(2), sz: 6.6pt, weight: "bold"), anchor: "west")
      content((x + 4.25, y + 1.37), _t(raw(s.at(3)), sz: 6pt, fill: cImg2), anchor: "east")
      // 미니 스케치
      let k = s.at(4)
      if k == "hist" {
        for i in range(18) {
          let h = 0.12 + 0.72 * calc.exp(-calc.pow((i - 8.5) / 4.0, 2))
          rect((x + 0.35 + i * 0.21, y + 0.2), (x + 0.5 + i * 0.21, y + 0.2 + h), fill: cImg2.lighten(45%), stroke: none)
        }
      } else if k == "hcut" or k == "vcut" {
        let pts = ()
        for i in range(23) {
          let t = i / 22.0
          pts.push((x + 0.35 + t * 3.7, y + 0.5 + 0.42 * calc.sin(t * 9) * calc.exp(-t * 0.7)))
        }
        line(..pts, stroke: 0.6pt + cImg2)
        let pts2 = ()
        for i in range(23) {
          let t = i / 22.0
          pts2.push((x + 0.35 + t * 3.7, y + 0.42 + 0.36 * calc.sin(t * 9 + 0.4) * calc.exp(-t * 0.7)))
        }
        line(..pts2, stroke: (paint: cImg3, thickness: 0.6pt, dash: "dashed"))
      } else if k == "stat" {
        for (i, s2) in ("mean  128.4", "std    12.9", "min/max  0 / 255").enumerate() {
          content((x + 0.35, y + 0.85 - i * 0.3), _t(raw(s2), sz: 5.4pt), anchor: "west")
        }
      } else if k == "roi" {
        rect((x + 0.35, y + 0.2), (x + 2.1, y + 1.05), fill: luma(240), stroke: 0.4pt + cLine)
        rect((x + 0.9, y + 0.42), (x + 1.6, y + 0.85), stroke: (paint: cOk, thickness: 0.7pt, dash: "dashed"))
        content((x + 2.25, y + 0.80), _t("선택 영역 통계", sz: 5.4pt), anchor: "west")
        content((x + 2.25, y + 0.48), _t("+ xy · CCT", sz: 5.4pt), anchor: "west")
      } else if k == "scat" {
        line((x + 0.35, y + 0.2), (x + 1.75, y + 1.05), stroke: 0.4pt + cLine)
        for i in range(26) {
          let t = i / 25.0
          let dx = 0.06 * calc.sin(i * 2.7)
          circle((x + 0.35 + t * 1.4 + dx, y + 0.2 + t * 0.85 + 0.05 * calc.cos(i * 3.1)),
                 radius: 0.035, fill: cImg2, stroke: none)
        }
        content((x + 2.0, y + 0.62), _t("A 값 vs B 값", sz: 5.4pt), anchor: "west")
      }
    }
    // 연결선
    for s in sats {
      let cx = s.at(0) + 2.2
      let cy = if s.at(1) > 3 { s.at(1) } else { s.at(1) + 1.55 }
      line((cx, cy), (calc.clamp(cx, 6.3, 9.7), if s.at(1) > 3 { 4.35 } else { 2.85 }),
           stroke: (paint: cLine, thickness: 0.5pt, dash: "dotted"))
    }
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 11 — 시퀀스 동기화
// ─────────────────────────────────────────────────────────────
#let fig_sequence_sync() = _fig(
  [디렉토리 시퀀스 동기 이동. #raw("--pair") 는 두 디렉토리를, #raw("--comp") 는 세 디렉토리를 같은 파일명으로 묶는다.],
  canvas(length: 1cm, {
    import draw: *
    let files = ("f001.png", "f002.png", "f003.png", "f004.png")
    let dirbox(x, y, title, col, cur) = {
      rect((x, y), (x + 3.2, y + 2.65), fill: white, stroke: 0.7pt + col)
      rect((x, y + 2.3), (x + 3.2, y + 2.65), fill: col.lighten(82%), stroke: 0.7pt + col)
      content((x + 0.15, y + 2.47), _t(raw(title), sz: 6.2pt, weight: "bold"), anchor: "west")
      for (i, f) in files.enumerate() {
        let fy = y + 1.95 - i * 0.5
        if i == cur {
          rect((x + 0.12, fy - 0.19), (x + 3.08, fy + 0.19), fill: col.lighten(88%), stroke: 0.5pt + col)
        }
        content((x + 0.28, fy), _t(raw(f), sz: 5.9pt,
          fill: if i == cur { col.darken(20%) } else { cSoft }), anchor: "west")
      }
    }
    // 위: --pair
    content((0.15, 7.35), _t([#raw("--pair") \u{2014} A · B 두 디렉토리], sz: 7.6pt, weight: "bold"), anchor: "west")
    dirbox(0.15, 4.4, "dirA/", cImg2, 1)
    dirbox(4.55, 4.4, "dirB/", cImg3, 1)
    line((3.35, 5.85), (4.55, 5.85), stroke: (paint: cOk, thickness: 0.8pt, dash: "dashed"))
    content((3.95, 6.12), _t("같은 이름", sz: 5.4pt, fill: cOk))
    _ar((2.0, 4.28), (2.0, 3.85), stroke: 0.8pt + cInk)
    content((2.35, 4.06), _t([#raw(";") 다음 / #raw("a") 이전], sz: 6.2pt), anchor: "west")
    content((5.15, 4.06), _t("두 패널이 함께 이동", sz: 6.2pt, fill: cSoft), anchor: "west")

    // 아래: --comp
    content((8.6, 7.35), _t([#raw("--comp") \u{2014} 원본 · img2 · img3 세 디렉토리], sz: 7.6pt, weight: "bold"), anchor: "west")
    dirbox(8.6, 4.4, "orig/", cOrig, 1)
    dirbox(12.5, 4.4, "img2/", cImg2, 1)
    dirbox(8.6, 0.9, "img3/", cImg3, 1)
    line((11.8, 5.85), (12.5, 5.85), stroke: (paint: cOk, thickness: 0.8pt, dash: "dashed"))
    line((10.2, 4.4), (10.2, 3.55), stroke: (paint: cOk, thickness: 0.8pt, dash: "dashed"))
    content((12.35, 3.9), _t([#raw(";") 를 누르면 원본 기준으로], sz: 6.2pt), anchor: "west")
    content((12.35, 3.55), _t("세 패널이 같은 파일명으로", sz: 6.2pt), anchor: "west")
    content((12.35, 3.2), _t("동시에 다음 프레임으로 이동", sz: 6.2pt), anchor: "west")
    content((12.35, 2.6), _t("파일명이 없으면 해당 패널은", sz: 6.2pt, fill: cSoft), anchor: "west")
    content((12.35, 2.25), _t("이전 프레임을 유지한다", sz: 6.2pt, fill: cSoft), anchor: "west")

    // 좌하단 설명
    rect((0.15, 0.9), (8.0, 3.55), fill: luma(250), stroke: 0.45pt + cLine)
    content((0.4, 3.25), _t("동기 이동이 필요한 이유", sz: 7pt, weight: "bold"), anchor: "west")
    for (i, s) in (
      "\u{2022} 보상 IP 검증은 프레임 단위 회귀 추적이 핵심이다.",
      "\u{2022} 같은 이름 규칙(f001.png ...)만 지키면 디렉토리 구조는 자유롭다.",
      "\u{2022} 활성 패널만 넘기고 싶으면 Tab 으로 패널을 바꾼 뒤 ; / a 를 쓴다.",
      "\u{2022} 슬라이드쇼(Shift+A)로 자동 재생하면 깜빡임 비교가 된다.",
      "\u{2022} 로딩 직후 파일명이 상단에 1.5초 토스트로 표시된다.",
    ).enumerate() {
      content((0.4, 2.85 - i * 0.42), _t(s, sz: 6.3pt), anchor: "west")
    }
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 12 — CI 파이프라인
// ─────────────────────────────────────────────────────────────
#let fig_ci_flow() = _fig(
  [헤드리스 모드의 CI 연동 흐름. 창을 띄우지 않고 종료 코드로 합격/불합격을 알린다.],
  canvas(length: 1cm, {
    import draw: *
    _box((0.15, 3.4), (2.9, 4.45), [보상 IP 출력\ (렌더 결과)], fill: luma(240), sz: 6.8pt)
    _ar((2.9, 3.92), (3.7, 3.92))
    _box((3.7, 3.4), (7.4, 4.45), [#raw("av --metrics A B")\ #raw("--format junit")], fill: cImg2.lighten(88%), stroke: 0.7pt + cImg2, sz: 6.8pt)
    _ar((7.4, 3.92), (8.2, 3.92))
    _box((8.2, 3.4), (10.6, 4.45), [CSV / JSON\ / JUnit XML], fill: luma(240), sz: 6.8pt)
    _ar((10.6, 3.92), (11.4, 3.92))
    // 판정 마름모
    line((11.4, 3.92), (12.7, 4.72), (14.0, 3.92), (12.7, 3.12), close: true,
         fill: rgb("#fef3c7"), stroke: 0.7pt + rgb("#b45309"))
    content((12.7, 3.92), _t([게이트\ 위반?], sz: 6.4pt))
    _ar((12.7, 4.72), (12.7, 5.35), stroke: 0.7pt + cBad)
    _box((11.2, 5.35), (14.2, 6.15), [exit 10 \u{2014} FAIL], fill: cBad.lighten(85%), stroke: 0.7pt + cBad, sz: 7pt, tc: cBad.darken(15%), w: "bold")
    _ar((12.7, 3.12), (12.7, 2.5), stroke: 0.7pt + cOk)
    _box((11.2, 1.7), (14.2, 2.5), [exit 0 \u{2014} PASS], fill: cOk.lighten(85%), stroke: 0.7pt + cOk, sz: 7pt, tc: cOk.darken(15%), w: "bold")

    // 게이트 목록
    rect((0.15, 0.15), (10.6, 2.85), fill: luma(250), stroke: 0.45pt + cLine)
    content((0.4, 2.55), _t("CI 게이트 옵션", sz: 7pt, weight: "bold"), anchor: "west")
    for (i, s) in (
      ("--fail-psnr <dB>", "PSNR 이 기준 미만이면 FAIL"),
      ("--warn-psnr <dB>", "PSNR 이 기준 미만이면 WARN (종료 코드는 유지)"),
      ("--fail-ssim <v>", "SSIM 이 기준 미만이면 FAIL"),
      ("--fail-flip <v>", "FLIP 이 기준 초과면 FAIL"),
      ("--fail-maxerr <v>", "최대 오차가 기준 초과면 FAIL"),
      ("--fail-uniformity / --fail-semu", "균일도·무라 게이트 (8장)"),
    ).enumerate() {
      let y = 2.15 - i * 0.36
      content((0.4, y), _t(raw(s.at(0)), sz: 6.1pt, fill: cImg2), anchor: "west")
      content((5.0, y), _t(s.at(1), sz: 6.1pt), anchor: "west")
    }
    content((5.55, 4.75), _t("창·GL·SDL 없이 실행 \u{2192} SSH·컨테이너에서 그대로 동작", sz: 6.2pt, fill: cSoft))
    content((5.55, 3.05), _t("stdout = 순수 데이터, 요약·경고 = stderr 로 분리", sz: 6.2pt, fill: cSoft))
  }),
)

// ─────────────────────────────────────────────────────────────
// 그림 13 — 색 파이프라인
// ─────────────────────────────────────────────────────────────
#let fig_color_pipeline() = _fig(
  [표시 파이프라인과 측정 경로의 분리. 룩(LUT/CDL)은 화면에만 적용되며 지표·픽셀 값 판독에는 영향을 주지 않는다.],
  canvas(length: 1cm, {
    import draw: *
    let ys = 4.2
    _box((0.15, ys), (2.75, ys + 1.0), [파일 디코드\ (PNG/HDR/PNM ...)], fill: luma(240), sz: 6.6pt)
    _ar((2.75, ys + 0.5), (3.45, ys + 0.5))
    _box((3.45, ys), (6.35, ys + 1.0), [색 관리\ #raw("--profile")\ #raw("--no-color-mgmt")], fill: luma(243), sz: 5.8pt)
    _ar((6.35, ys + 0.5), (7.05, ys + 0.5))
    _box((7.05, ys), (10.35, ys + 1.0), [룩 적용\ #raw("--lut") .cube (3D LUT)\ #raw("--cdl") ASC-CDL], fill: cImg3.lighten(88%), stroke: 0.7pt + cImg3, sz: 5.8pt)
    _ar((10.35, ys + 0.5), (11.05, ys + 0.5))
    _box((11.05, ys), (13.55, ys + 1.0), [채널 선택\ #raw("Shift+R/G/B/Y/C")], fill: luma(243), sz: 6.4pt)
    _ar((13.55, ys + 0.5), (14.25, ys + 0.5))
    _box((14.25, ys), (15.85, ys + 1.0), "화면 표시", fill: cOk.lighten(88%), stroke: 0.7pt + cOk, sz: 7pt)
    content((8.7, ys + 1.32), _t([#raw("'") 키로 룩 ON/OFF 토글], sz: 6.2pt, fill: cImg3))

    // 측정 경로
    line((1.45, ys), (1.45, 2.6), stroke: (paint: cOk, thickness: 0.7pt, dash: "dashed"))
    _ar((1.45, 2.6), (2.6, 2.6), stroke: 0.7pt + cOk)
    _box((2.6, 2.15), (8.3, 3.05), [측정·판독 경로 \u{2014} 원시 픽셀 그대로], fill: cOk.lighten(90%), stroke: 0.7pt + cOk, sz: 7pt)
    _ar((8.3, 2.6), (9.0, 2.6), stroke: 0.7pt + cOk)
    content((9.15, 2.75), _t([PSNR/SSIM/FLIP · #raw("V") 픽셀 값 · #raw("Shift+V") 컬러메트리], sz: 6.3pt), anchor: "west")
    content((9.15, 2.35), _t([#raw("--metrics") / #raw("--probe") / #raw("--comp-out")], sz: 6.3pt), anchor: "west")

    // 색 코어
    rect((0.15, 0.2), (15.85, 1.75), fill: luma(250), stroke: 0.45pt + cLine)
    content((0.45, 1.45), _t("공용 색 코어 (src/color.cpp) \u{2014} GUI 프로브와 헤드리스가 같은 계산을 쓴다", sz: 7pt, weight: "bold"), anchor: "west")
    content((0.45, 1.05), _t("sRGB EOTF \u{2192} 선형 RGB \u{2192} XYZ (sRGB/Rec.709 D65 행렬) \u{2192} xy · u\u{2032}v\u{2032} · CCT(McCamy) · Duv · L*a*b* · \u{0394}E76", sz: 6.4pt), anchor: "west")
    content((0.45, 0.62), _t("기준 백색 CIE 2\u{00B0} D65 = (0.95047, 1.00000, 1.08883).   FLIP 엔진은 레퍼런스 일치를 위해 자체 D65 를 쓰는 별도 경로다.", sz: 6.4pt), anchor: "west")
  }),
)
