// ─────────────────────────────────────────────────────────────
// av \u{2014} User's Manual  (마스터 문서)
//
//   컴파일:  typst compile doc_typst/av_users_manual.typ
//   본문 각 장은 doc_typst/manual_parts/chNN_*.typ 에 있다.
//   스타일·헬퍼·그림은 manual_parts/_common.typ 이 소유한다.
// ─────────────────────────────────────────────────────────────

#import "manual_parts/_common.typ": *
#import "_version.typ": AV_VERSION, AV_VERSION_DATE

// ── 문서 메타 ────────────────────────────────────────────────
#set document(
  title: "av User's Manual",
  author: "Alex",
  date: datetime(year: 2026, month: 7, day: 31),
)

// ── 페이지 ───────────────────────────────────────────────────
#set page(
  paper: "a4",
  margin: (top: 2.6cm, bottom: 2.3cm, left: 2.5cm, right: 2.5cm),
  numbering: "1",
  number-align: center,
  header: context {
    if counter(page).get().first() > 2 {
      set text(size: 8.5pt, fill: luma(120))
      grid(
        columns: (1fr, auto),
        align: (left, right),
        [av User's Manual], [#AV_VERSION],
      )
      v(-0.55em)
      line(length: 100%, stroke: 0.4pt + luma(190))
    }
  },
)

// ── 본문 텍스트 ──────────────────────────────────────────────
#set text(
  font: ("CMU Serif", "Noto Sans KR"),
  size: 10.5pt,
  weight: 300,
  lang: "ko",
)

#set par(justify: true, leading: 0.82em, spacing: 1.25em)

#show strong: it => text(weight: "bold", it)

// ── 제목 ─────────────────────────────────────────────────────
#set heading(numbering: "1.1.")

#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  counter(figure.where(kind: image)).update(0)
  v(0.6em)
  block(
    width: 100%,
    inset: (bottom: 0.45em),
    stroke: (bottom: 1.1pt + luma(60)),
    text(size: 19pt, weight: "bold", it),
  )
  v(0.9em)
}

#show heading.where(level: 2): it => {
  v(1.25em, weak: true)
  block(text(size: 13.5pt, weight: "bold", fill: luma(25), it))
  v(0.42em, weak: true)
}

#show heading.where(level: 3): it => {
  v(0.95em, weak: true)
  block(text(size: 11.2pt, weight: "bold", fill: rgb("#1e3a5f"), it))
  v(0.28em, weak: true)
}

// ── 코드 ─────────────────────────────────────────────────────
#show raw.where(block: false): it => box(
  fill: luma(240),
  inset: (x: 3.5pt, y: 1.5pt),
  outset: (y: 1.5pt),
  radius: 2.5pt,
  text(size: 0.88em, font: "CMU Typewriter Text", fill: rgb("#0f2b46"), it),
)

#show raw.where(block: true): it => block(
  width: 100%,
  fill: luma(247),
  inset: (x: 0.85em, y: 0.7em),
  radius: 3pt,
  stroke: 0.45pt + luma(200),
  spacing: 1.2em,
  text(size: 8.8pt, font: "CMU Typewriter Text", it),
)

// ── 표 ───────────────────────────────────────────────────────
#set table(
  inset: (x: 6pt, y: 4.5pt),
  stroke: (x, y) => (
    top: if y == 0 { 0.9pt + luma(70) } else if y == 1 { 0.6pt + luma(120) } else { 0.3pt + luma(215) },
    bottom: 0.9pt + luma(70),
    left: none, right: none,
  ),
  fill: (x, y) => if y == 0 { luma(236) } else if calc.odd(y) { luma(251) } else { white },
)

#show table: set text(size: 9.3pt)
#show table: set par(justify: false, leading: 0.62em)

// ── 목록 ─────────────────────────────────────────────────────
#set list(indent: 0.6em, spacing: 0.75em)
#set enum(indent: 0.6em, spacing: 0.75em)

// ── 그림 ─────────────────────────────────────────────────────
#set figure(numbering: n => {
  let ch = counter(heading).get()
  let c = if ch.len() > 0 { ch.first() } else { 0 }
  numbering("1.1", c, n)
})

#show figure: set block(spacing: 1.6em)
#show figure.caption: it => block(
  width: 92%,
  text(size: 9pt, fill: luma(70), it),
)

// ═════════════════════════════════════════════════════════════
// 표지
// ═════════════════════════════════════════════════════════════
#page(numbering: none, header: none, {
  v(3.2cm)
  align(center, {
    text(size: 46pt, weight: "bold")[av]
    v(-0.35em)
    text(size: 15pt, fill: luma(90))[Advanced Pixel Lens]
    v(1.5em)
    line(length: 42%, stroke: 0.9pt + luma(60))
    v(1.2em)
    text(size: 27pt, weight: "bold")[User's Manual]
    v(0.5em)
    text(size: 12pt, fill: luma(80))[이미지 뷰어 · 비교기 · 화질 검증 도구 사용 설명서]
    v(2.2em)
    // 표지 그래픽 — 3패널 비교 모드
    canvas(length: 1cm, {
      import draw: *
      let W = 3.3
      let names = ("orig", "img2", "img3")
      let cols = (cOrig, cImg2, cImg3)
      for i in range(3) {
        let x0 = i * (W + 0.3)
        rect((x0, 0), (x0 + W, 2.3), fill: luma(248), stroke: 1.0pt + cols.at(i))
        rect((x0, 2.3), (x0 + W, 2.68), fill: cols.at(i).lighten(80%), stroke: 1.0pt + cols.at(i))
        content((x0 + W / 2, 2.49), _t(names.at(i), sz: 8pt, fill: cols.at(i).darken(15%), weight: "bold"))
      }
      // 중간 패널의 worst 블록
      let x2 = W + 0.3
      for b in ((0.5, 1.55, cWorst), (1.9, 0.85, rgb("#f97316")), (2.5, 1.75, cWarm), (1.1, 0.45, cSpike)) {
        rect((x2 + b.at(0), b.at(1)), (x2 + b.at(0) + 0.34, b.at(1) + 0.34), stroke: 1.1pt + b.at(2))
      }
      let x3 = 2 * (W + 0.3)
      for b in ((0.5, 1.55, cEcho),) {
        rect((x3 + b.at(0), b.at(1)), (x3 + b.at(0) + 0.34, b.at(1) + 0.34), stroke: 1.1pt + b.at(2))
      }
      rect((0.5, 1.55), (0.84, 1.89), stroke: 1.1pt + cEcho)
    })
    v(2.4em)
    text(size: 11pt, fill: luma(60))[#AV_VERSION #h(0.8em) · #h(0.8em) #AV_VERSION_DATE]
    v(0.6em)
    text(size: 9.5pt, fill: luma(120))[DDI 보상 IP 화질 검증을 위한 실무 안내서]
  })
})

// ═════════════════════════════════════════════════════════════
// 차례
// ═════════════════════════════════════════════════════════════
#page(numbering: none, header: none, {
  show outline.entry.where(level: 1): it => {
    v(0.85em, weak: true)
    strong(it)
  }
  outline(title: [차례], depth: 3, indent: 1.2em)
})

#page(numbering: none, header: none, {
  outline(title: [그림 차례], target: figure.where(kind: image))

  v(1.6em)
  block(
    width: 100%,
    fill: luma(249),
    stroke: 0.5pt + luma(200),
    inset: 1em,
    radius: 3pt,
    {
      text(size: 10.5pt, weight: "bold")[이 설명서를 읽는 법]
      v(0.5em)
      set text(size: 9.8pt)
      [
        - 처음 쓰는 사람은 1장 \u{2192} 2장 \u{2192} 3장 순서로 읽으면 기본 조작이 끝납니다.
        - 두 영상을 비교하려면 4장, 세 영상을 블록 단위로 비교하려면 5장을 봅니다.
        - 스크립트·CI 자동화가 목적이면 8장만 읽어도 됩니다.
        - 키를 빨리 찾으려면 11장 부록의 전체 단축키 표를 보거나, 실행 중에
          #key("Ctrl+Shift+H") 로 핫키 창을 여십시오.
        - 이 문서의 모든 예시는 실제로 동작하는 명령 문법입니다.
      ]
    },
  )
})

// ═════════════════════════════════════════════════════════════
// 본문
// ═════════════════════════════════════════════════════════════
#counter(page).update(1)

#include "manual_parts/ch01_intro.typ"
#include "manual_parts/ch02_view.typ"
#include "manual_parts/ch03_pixel.typ"
#include "manual_parts/ch04_diff.typ"
#include "manual_parts/ch05_comp.typ"
#include "manual_parts/ch06_analysis.typ"
#include "manual_parts/ch07_sequence.typ"
#include "manual_parts/ch08_headless.typ"
#include "manual_parts/ch09_color.typ"
#include "manual_parts/ch10_config.typ"
#include "manual_parts/ch11_appendix.typ"
