#import "_common.typ": *

= 부록 \u{2014} 전체 레퍼런스

이 장은 설명이 아니라 *찾아보기* 를 위한 장입니다. 앱 안의 핫키 창
(#key("Ctrl+Shift+H"))과 `av --help` 에 실려 있는 항목을 하나도 빠뜨리지 않고
표로 옮겼고, 헤드리스 출력의 열 이름, 종료 코드, 지원 포맷, 용어 정의를 덧붙였습니다.
각 항목의 배경과 실전 활용은 표의 "관련 장" 열이 가리키는 장을 보십시오.

== 키보드 단축키 전수표

아래 표는 `src/ui/main_window.cpp` 의 `render_hotkey_help_window()` 가 앱 안에서
보여 주는 목록과 *1:1 로 대응* 합니다. 카테고리 이름도 앱 창의 `Category` 열과
같습니다. 비고 열에는 소스(`src/app.cpp` 의 스캔코드 처리)에서 확인한 모드 제약과
주의점을 적었습니다.

#note[표기 규칙 두 가지입니다. 첫째, #key("Cmd") 는 SDL 의 GUI 수식키로,
macOS 에서는 Command, Windows/Linux 에서는 Super(Win) 키입니다. 둘째, 텍스트 입력
위젯에 포커스가 있을 때는 문자 키가 그 위젯으로 먼저 들어갑니다 (복사 모드 진입은
`WantTextInput` 을 확인해 이 경우에만 막습니다).]

=== Navigation \u{2014} 이동

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("H") / #key("Left")], [왼쪽으로 1픽셀 팬], [이미지 좌표 기준. 줌 배율과 무관하게 항상 1픽셀 (2장)],
  [#key("L") / #key("Right")], [오른쪽으로 1픽셀 팬], [`sync` 가 켜져 있으면 A/B 가 함께 움직입니다],
  [#key("K") / #key("Up")], [위로 1픽셀 팬], [\u{2014}],
  [#key("J") / #key("Down")], [아래로 1픽셀 팬], [\u{2014}],
  [#key("Shift") + #key("H") #key("L") #key("K") #key("J") / 방향키], [빠른 팬 (`pan_step` 픽셀)], [기본 32픽셀. `-p` / `--pan-step` 으로 변경. #key("Shift+Up")/#key("Shift+Down") 은 슬라이드쇼가 켜져 있으면 간격 조절로 가로챕니다],
  [#key("Cmd+Shift") + #key("H") #key("L") #key("K") #key("J")], [해당 방향 끝단으로 점프], [`fit` 이 자동으로 해제됩니다. Windows/Linux 에서는 Super(Win) 키],
  [#key("G")], [이미지 중심으로], [`--comp` 모드에서는 #key("G") 가 *블록 그리드 토글* 로 바뀝니다 (5장)],
  [마우스 좌드래그], [이미지 팬], [ROI 모드(#key("Ctrl+E")) 와 Curtain 모드에서는 드래그가 그쪽으로 소비됩니다],
)

=== Zoom \u{2014} 줌

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("+") / #key("=") / #key("Numpad+")], [줌 인 (2배)], [줌 단계 목록에서 한 칸 위 (2장)],
  [#key("-") / #key("Numpad-")], [줌 아웃 (1/2배)], [\u{2014}],
  [#key("Z")], [줌 인], [왼손 조작용 대체 키],
  [#key("Shift+Z")], [줌 아웃], [#key("X") 도 같은 동작을 합니다 (핫키 창 목록에는 없음)],
  [#key("0")], [1:1 (100%) \u{002B} 중앙 정렬], [`2^0 = 1`. 픽셀 등배로 볼 때],
  [#key("1") \u{2013} #key("8")], [`2^n` 배율 (1=2배 \u{2026} 8=256배)], [항상 중앙 정렬이 함께 적용됩니다. #key("Ctrl") 을 같이 누르면 diff 모드 전환이 됩니다],
  [#key("F")], [fit 토글], [끄면 직전 배율로 복귀],
  [#key("Space")], [창에 맞춤(fit) \u{002B} 중앙 정렬], [#key("F") 는 fit 토글. #key("Shift+Space") 는 A/B 스왑],
  [마우스 휠], [줌 인/아웃], [#key("Alt") 를 같이 누르면 시퀀스 이동으로 바뀝니다],
  [마우스 우드래그], [선택 영역으로 줌], [드래그한 사각형이 패널을 채우도록 배율\u{00B7}팬을 계산합니다],
)

=== Display \u{2014} 표시

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("U")], [UI 오버레이(메뉴바/상태바) 토글], [기본값 off. `~/.av.ini` 에 저장됩니다],
  [#key("I")], [Image Info 창 토글], [`--comp` 에서는 img2\u{00B7}img3 의 원본 대비 PSNR 이 함께 표시됩니다],
  [#key("V")], [커서 픽셀값 풍선말 토글], [3장],
  [#key("S")], [뷰포트 sync 토글 (A \u{2194} B)], [`--comp` 모드에서는 *img2 \u{2194} img3 패널 교환* 으로 바뀝니다],
  [#key("W")], [윈도우 모드 토글 (타이틀바)], [끄면 borderless \u{002B} maximized. `--windowed` 로 시작 상태 지정],
  [#key("Tab")], [활성 패널 전환 (A \u{2194} B)], [`--comp` 에서는 worst 블록 순회로 바뀝니다 (5장)],
  [#key("R")], [이미지 시계방향 90도 회전], [두 패널 모두 회전하고 뷰포트는 fit \u{002B} 중앙으로 초기화],
  [#key("Ctrl+R")], [이미지 반시계방향 90도 회전], [#key("Shift+R") 은 Red 채널이므로 혼동 주의],
  [#key("Shift+Space")], [A/B 영상 스왑], [두 영상이 모두 로드된 경우에만 동작],
  [#key("M")], [크로스헤어 오버레이 토글], [`~/.av.ini` 에 저장],
  [#key("Ctrl+M")], [돋보기 토글], [diff 모드가 아닐 때만. Enhance 모드에는 자체 돋보기가 있습니다 (4장)],
  [#key("P")], [Pathfinder: 이미지 미니맵], [fit 상태에서는 표시되지 않습니다],
  [#key("Ctrl+P")], [Pathfinder: 개략도(schematic) 모드], [같은 키를 다시 누르면 숨김],
  [#key("B")], [패널 테두리 토글], [`-nb` 로 숨긴 채 시작 가능. `--comp` 에서 #key("Ctrl+B") 는 worst 박스],
  [#key("/")], [불량 픽셀 오버레이 토글], [float/HDR 영상의 NaN\u{00B7}Inf\u{00B7}음수\u{00B7}`>1` 표시. 헤드리스 대응은 `--validate`],
  [#key("Ctrl+X")], [픽셀값 표기 형식 순환], [Decimal \u{2192} `0x80` \u{2192} `80h`. #key("X") 단독은 줌 아웃 (3장)],
)

=== Channel \u{2014} 채널

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("Shift+R")], [Red 채널만 표시], [채널 모드는 diff 픽셀 리스트의 필터로도 쓰입니다 (4장)],
  [#key("Shift+G")], [Green 채널만 표시], [\u{2014}],
  [#key("Shift+B")], [Blue 채널만 표시], [\u{2014}],
  [#key("Shift+Y")], [Rec.709 루마(Y\u{2032}) 그레이스케일 토글], [다시 누르면 RGB 로 복귀 (토글형)],
  [#key("Shift+C")], [RGB 표시 (기본값)], [채널 모드는 `~/.av.ini` 에 저장됩니다],
)

=== Analysis \u{2014} 분석

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("Ctrl+H")], [히스토그램 창 토글], [열면 라인컷\u{00B7}통계 창은 자동으로 닫힙니다 (배타적) \u{2014} 6장],
  [#key("Ctrl+L")], [H-Line Cut 토글], [커서가 놓인 행의 수평 프로파일],
  [#key("Ctrl+Y")], [V-Line Cut 토글], [커서가 놓인 열의 수직 프로파일],
  [#key("Ctrl+S")], [통계 창 토글], [#key("Shift+Ctrl+S") 는 저장 다이얼로그이므로 구분 주의],
  [#key("Ctrl+E")], [ROI 선택 모드 토글 (좌드래그로 선택)], [모드를 끄면 선택 영역도 함께 초기화됩니다],
  [#key("Shift+V")], [컬러메트리 프로브 (xy / u\u{2032}v\u{2032} / CCT) 를 풍선말에 추가], [켜면 픽셀 풍선말(#key("V"))도 자동으로 켜집니다 (3장, 9장)],
  [#key("Ctrl+T")], [산점도(Scatter Plot) 토글], [A 픽셀값 대 B 픽셀값의 상관 산점도],
)

=== Overlay \u{2014} 오버레이

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("O")], [Overlay/Blend 비교 모드 토글], [두 영상을 한 캔버스에 겹칩니다. `~/.av.ini` 에 저장],
  [Curtain 모드], [좌드래그로 분할선 이동], [모드 선택은 메뉴 View \u{2192} Overlay \u{2192} `Blend mode` / `Curtain mode`. 분할선 기본 위치는 0.5],
)

=== Sequence \u{2014} 시퀀스

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key(";")], [디렉토리 시퀀스 다음 영상 (활성 패널)], [`--pair` / `--comp` 에서는 상대 패널도 같은 파일명으로 따라옵니다 (7장)],
  [#key("A")], [디렉토리 시퀀스 이전 영상 (활성 패널)], [#key("Ctrl") \u{00B7} #key("Cmd") 조합에서는 동작하지 않습니다],
  [#key("N") / #key("Shift+N")], [다음 / 이전 영상 (구형 핫키)], [#key(";") / #key("A") 와 완전히 동일한 동작],
  [#key("Tab")], [활성 패널 전환 (A \u{2194} B)], [Display 카테고리와 같은 키. `--comp` 에서는 블록 순회],
  [#key("Alt") \u{002B} 마우스 휠], [커서 아래 패널의 영상 이동], [활성 패널이 아니라 *커서가 놓인* 패널이 대상입니다],
  [#key("Shift+A")], [슬라이드쇼 자동 재생 토글], [시작할 때의 활성 패널이 대상 패널로 고정됩니다],
  [#key("Shift+Up")], [슬라이드쇼 간격 \u{002B}1초], [최대 10초. 슬라이드쇼가 꺼져 있으면 팬으로 동작],
  [#key("Shift+Down")], [슬라이드쇼 간격 \u{2212}1초], [최소 0.5초],
)

=== Blink \u{2014} 블링크 비교기

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key(",")], [블링크 비교기 토글 (A/B 자동 교대)], [두 영상이 모두 로드된 경우에만. 켜는 순간 두 뷰포트가 활성 패널 값으로 정렬되고 sync 가 강제로 켜집니다 (끄면 원래 sync 로 복원)],
  [#key("<") / #key(">") (#key("Shift+,") / #key("Shift+."))], [블링크 간격 \u{2212}/\u{002B}0.1초], [범위 0.1 \u{2013} 2.0초. 블링크(또는 `--comp` 3-way 블링크)가 켜져 있을 때만 반응],
)

=== Diff \u{2014} 차이 비교

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("Ctrl+D")], [diff 픽셀 리스트 창 토글], [`--comp` 모드에서는 *worst 블록 리스트 창* 으로 바뀝니다],
  [#key("Ctrl+2")], [Diff: Alpha Blend 토글], [B 가 없으면 경고 토스트가 뜹니다],
  [#key("Ctrl+3")], [Diff: Pixel Absolute 토글], [`-d` / `--diff` 로 시작 시 이 모드],
  [#key("Ctrl+4")], [Diff: Pixel Relative 토글], [\u{2014}],
  [#key("Ctrl+5")], [Diff: Enhance 토글 (remap \u{002B} 돋보기)], [\u{2014}],
  [#key("Ctrl+6")], [Diff: False Color 토글], [\u{2014}],
  [#key("Ctrl+7")], [Diff: SSIM 토글], [\u{2014}],
  [#key("Ctrl+0")], [Diff: FLIP 토글 (지각 오차맵, magma)], [\u{2014}],
  [#key("Ctrl+1")], [Diff: Signed 토글 (파랑=A\<B, 흰색=동일, 빨강=A\>B)], [\u{2014}],
  [#key("Ctrl+9")], [Diff: Highlight 토글 (빨강)], [\u{2014}],
  [#key("[") / #key("]")], [주 diff 파라미터 \u{2212}/\u{002B}], [일반 모드: `amplify` \u{00B1}0.5 (범위 0.5 \u{2013} 100). AlphaBlend: alpha \u{00B1}1%],
  [#key("Shift+[") / #key("Shift+]")], [보조 파라미터 \u{2212}/\u{002B}], [일반 모드: threshold \u{00B1}1 (범위 0 \u{2013} 255). AlphaBlend: alpha \u{00B1}10%],
  [#key("\\")], [주 diff 파라미터 리셋], [`amplify` = 1.0, AlphaBlend 는 alpha = 50%],
  [#key("Ctrl+8")], [허용오차(tolerance) 기반 diff 토글], [threshold 를 켜고 끄는 스위치],
  [#key("Ctrl+\\")], [diff threshold 리셋 (0)], [\u{2014}],
)

#warn[`--comp` 모드에서는 diff 엔진이 통째로 비활성화됩니다. 위 Diff 키 중
#key("Ctrl+D") \u{00B7} #key("[") \u{00B7} #key("]") \u{00B7} #key("Shift+[") \u{00B7}
#key("Shift+]") 는 모두 comp 전용 동작으로 재배치됩니다. 4장과 5장을 함께 보십시오.]

=== Comp \u{2014} 3영상 블록 비교 (`--comp`)

#table(
  columns: (auto, auto, 1fr),
  [*키 / 옵션*], [*동작*], [*비고*],
  [`av --comp <orig> <img2> <img3>`], [3영상 비교: orig \| img2 \| img3 (diff 비활성)], [핫키 창에도 CLI 형태로 실려 있는 항목입니다],
  [`--blk <WxH>`], [worst-PSNR 탐색 블록 크기], [기본 `8x8`. 범위 2 \u{2013} 1024. `8` 처럼 한 값만 주면 `8x8` 로 해석],
  [`--num_blk <N>`], [윤곽선을 그릴 worst 블록 개수], [기본 16. 최소 1 로 클램프. `--num-blk` 도 같은 옵션],
  [#key("Ctrl+B")], [worst 블록 박스 토글], [빨강=worst \u{2192} 노랑 순. 마젠타 `P` 는 피크 픽셀 스파이크 픽],
  [#key("G")], [전체 블록 그리드 경계선 토글], [셀 크기는 `--grid` 값 (기본 `16x16`), `--blk` 와 독립],
  [#key("Ctrl+W")], [승패(win/loss) 맵], [파랑=img2 우세, 주황=img3 우세. 판정 기준 `|d| > 1dB`],
  [#key("Ctrl+G")], [블록 PSNR 히트맵 (img2/img3)], [노랑 \u{2192} 빨강 = 나쁨. 50dB 이상은 투명],
  [#key("Ctrl+D")], [worst 블록 리스트 창], [행을 클릭하면 세 패널이 동시에 그 블록으로 점프],
  [#key("S")], [img2 \u{2194} img3 패널 교환], [통계도 함께 따라갑니다 (슬롯 교환 후 재계산)],
  [#key("Tab") / #key("Shift+Tab")], [마우스가 놓인 패널의 worst 블록 순회], [패널 밖이면 img2\u{002B}img3 병합 심각도 순],
  [#key("[") / #key("]")], [img2(가운데) 의 worst 블록만 이전/다음], [마우스 위치와 무관하게 대상 고정],
  [#key("Shift+[") / #key("Shift+]")], [img3(오른쪽) 의 worst 블록만 이전/다음], [\u{2014}],
  [#key(",")], [3-way 블링크: 한 패널에서 orig \u{2192} img2 \u{2192} img3], [간격 조절은 #key("<") / #key(">")],
  [#key(";") / #key("A")], [orig 디렉토리의 다음/이전 영상], [img2\u{00B7}img3 는 같은 파일명으로 미러링],
  [블록 박스 위에 마우스 올리기], [상세 풍선말], [블록 PSNR/MSE, 상대 영상의 같은 블록 PSNR, 채널별 MSE, 최악 픽셀의 orig/현재/차이. 반대편 비교 패널에는 *그 패널 자신의 통계* 로 같은 형식의 풍선말이 뜨고, orig 패널에는 박스만 표시],
  [#key("I")], [Info 창: img2 와 img3 의 원본 대비 PSNR], [\u{2014}],
)

=== File / Copy / Help

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("Shift+Ctrl+O")], [이미지 파일 열기], [활성 패널에 로드. macOS 는 #key("Shift+Cmd+O") 도 동작],
  [#key("Shift+Ctrl+S")], [저장 다이얼로그], [열려 있는 분석 창에 따라 이미지/차트/통계 저장 창이 선택됩니다 (7장)],
  [#key("Q")], [종료], [확인 창 없이 즉시. 종료 직전에 `~/.av.ini` 가 기록됩니다],
  [#key("Ctrl/Cmd+C") \u{2192} #key("1")], [Image A 를 PNG 로 클립보드 복사], [복사 모드는 5초 후 자동 취소. #key("Esc") 로도 취소],
  [#key("Ctrl/Cmd+C") \u{2192} #key("2")], [Image B 를 PNG 로 클립보드 복사], [숫자 키패드 #key("1")/#key("2")/#key("3") 도 인식],
  [#key("Ctrl/Cmd+C") \u{2192} #key("3")], [Diff 영상을 PNG 로 클립보드 복사], [현재 diff 모드의 결과 이미지],
  [#key("Ctrl+Shift+H")], [이 핫키 레퍼런스 창 토글], [열림 상태가 `~/.av.ini` 에 저장되므로 다음 실행에도 유지됩니다],
)

=== 핫키 창 목록에는 없지만 실제로 동작하는 키

소스의 스캔코드 처리에는 있으나 핫키 창 `entries[]` 에는 실려 있지 않은 키입니다.

#table(
  columns: (auto, auto, 1fr),
  [*키*], [*동작*], [*비고*],
  [#key("Esc")], [열려 있는 것을 하나씩 닫기], [블링크 \u{2192} diff 리스트 \u{2192} 핫키 창 \u{2192} 히스토그램/라인컷/통계 \u{2192} ROI 통계/산점도 \u{2192} Info/풍선말 \u{2192} 저장창/크로스헤어 \u{2192} ROI 모드/Overlay 순 (1장)],
  [#key("X")], [줌 아웃], [#key("Z") 와 짝. #key("Ctrl+X") 는 픽셀값 형식 순환],
  [#key("'")], [3D LUT / CDL 룩 표시 토글], [`--lut` 또는 `--cdl` 로 파일이 로드된 경우에만 반응. `av --help` 의 `--lut` 설명에만 적혀 있습니다 (9장)],
  [#key("Numpad 1") #key("Numpad 2") #key("Numpad 3")], [복사 모드에서 A / B / Diff 선택], [#key("Ctrl/Cmd+C") 로 복사 모드에 들어간 뒤 숫자열 키와 동일하게 동작],
)

== CLI 옵션 전수표

기본 문법입니다. 옵션과 이미지 경로의 순서는 자유이며, 위치 인자는 등장 순서대로
imageA \u{2192} imageB \u{2192} imageC 로 배정됩니다 (최대 3개, 초과 시 오류).

```
av [options] [imageA] [imageB]
av --comp [options] <orig> <img2> <img3>
```

=== GUI 옵션 \u{2014} 창과 표시

#table(
  columns: (auto, auto, auto, 1fr, auto),
  [*옵션*], [*인자*], [*기본값*], [*설명*], [*장*],
  [`--fullscreen`], [\u{2014}], [off], [전체 화면으로 시작], [1장],
  [`--windowed`], [\u{2014}], [off], [타이틀바 \u{002B} 크기 조절 가능한 창으로 시작], [1장],
  [`--geometry`], [`<WxH>`], [`1280x720`], [초기 창 크기], [1장],
  [`--software`], [\u{2014}], [off], [SDL 소프트웨어 렌더러 강제 (OpenGL 미사용)], [10장],
  [`-nb`], [\u{2014}], [off], [패널 테두리를 숨긴 채 시작], [1장],
  [`-bc`], [`<A> <B> <D>`], [`ff00ff ffff00 00ffff`], [A/B/Diff 패널 테두리 색 (6자리 hex, 알파 230 고정)], [1장],
  [`--zoom`], [`fit|0|1|2 ...`], [`.av.ini` 저장값 (없으면 fit)], [초기 배율. `0`/`fit`=창 맞춤, `1`=1:1. 지정 시 저장값을 덮어쓰고 이후 실행에도 유지], [2장],
  [`--sync`], [\u{2014}], [on], [A/B 뷰포트 동기화 켜기], [2장],
  [`--no-sync`], [\u{2014}], [\u{2014}], [A/B 뷰포트 동기화 끄기], [2장],
  [`-p`, `--pan-step`], [`<N>`], [`32`], [#key("Shift") \u{002B} hjkl 의 점프 크기 (이미지 픽셀)], [2장],
)

=== GUI 옵션 \u{2014} 비교와 색

#table(
  columns: (auto, auto, auto, 1fr, auto),
  [*옵션*], [*인자*], [*기본값*], [*설명*], [*장*],
  [`--diff-mode`], [`none|alphablend|abs|rel|highlight|falsecolor|ssim|flip|signed|enhance`], [`none`], [시작 시 diff 모드. 목록에 없는 값은 오류], [4장],
  [`-d`, `--diff`], [\u{2014}], [\u{2014}], [`--diff-mode abs` 의 단축형], [4장],
  [`--amplify`], [`<val>`], [`1.0`], [diff 증폭. 도움말 표기 범위 0.1 \u{2013} 100 (GUI 의 #key("[")/#key("]") 조절 범위는 0.5 \u{2013} 100)], [4장],
  [`--pair`], [\u{2014}], [off], [imageB 디렉토리에서 같은 파일명을 짝지어 시퀀스를 연동. *두 디렉토리가 서로 달라야* 합니다], [7장],
  [`--comp`], [\u{2014}], [off], [3영상 블록 비교. 이미지 3장 필수, diff 는 강제로 `none`. 세 번째 위치 인자를 주면 자동 활성화], [5장],
  [`--blk`], [`<WxH>` 또는 `<N>`], [`8x8`], [comp 블록 크기. 각 변 2 \u{2013} 1024], [5장],
  [`--num_blk`], [`<N>`], [`16`], [윤곽선을 그릴 worst 블록 개수 (최소 1). `--num-blk` 별칭], [5장],
  [`--blk-metric`], [`rgb|y|chroma`], [`rgb`], [worst 랭킹 기준. 리스트는 지표 MSE 75% \u{002B} 피크 픽셀 25%], [5장],
  [`--grid`], [`[WxH|N]` (선택)], [off / 크기 `16x16`], [시작 시 블록 그리드 표시. 값은 2 \u{2013} 1024 범위의 숫자 형태일 때만 소비 (파일명 오인 방지)], [5장],
  [`--profile`], [`<file>`], [\u{2014}], [ICC 색 프로파일 경로], [9장],
  [`--no-color-mgmt`], [\u{2014}], [off], [색 관리 비활성화], [9장],
  [`--lut`], [`<file.cube>`], [\u{2014}], [3D/1D `.cube` LUT 를 두 패널 표시에 적용 (#key("'") 토글)], [9장],
  [`--cdl`], [`<file.cdl>`], [\u{2014}], [ASC-CDL(`.cdl`/`.ccc`) slope/offset/power \u{002B} sat 룩. `--lut` 가 있으면 `--lut` 우선], [9장],
  [`--version`], [\u{2014}], [\u{2014}], [버전과 갱신 날짜를 출력하고 종료 (exit 0)], [1장],
  [`-h`, `--help`], [\u{2014}], [\u{2014}], [도움말을 출력하고 종료 (exit 0)], [1장],
)

=== 헤드리스 옵션

아래 옵션이 하나라도 있으면 창\u{00B7}OpenGL\u{00B7}SDL 을 열지 않고 계산 후 즉시
종료합니다. 여러 개를 동시에 주면 `--batch` \u{2192} `--comp-batch` \u{2192}
`--comp-out` \u{2192} `--metrics` \u{2192} `--diff-out` \u{2192} `--validate`
\u{2192} `--probe` \u{2192} `--uniformity` 순으로 먼저 걸린 하나만 실행됩니다.

#table(
  columns: (auto, auto, auto, 1fr, auto),
  [*옵션*], [*인자*], [*기본값*], [*설명*], [*장*],
  [`--metrics`], [\u{2014}], [off], [A 대 B 의 PSNR/SSIM/FLIP/MSE/MAE 를 16열 CSV 로 출력. `--pair` 와 함께 쓰면 시퀀스 전체를 프레임당 한 줄로], [8장],
  [`--batch`], [`<file|->`], [\u{2014}], [매니페스트의 임의 A,B 쌍을 지표 계산. TAB 구분 `A<TAB>B[<TAB>label]`, `#` 주석, `-` = stdin. 같은 파일명 제약 없음], [8장],
  [`--format`], [`csv|json|junit`], [`csv`], [`--metrics` / `--batch` 출력 형식. `--comp-out` 은 `csv|json` 만 (junit 지정 시 csv 로 대체)], [8장],
  [`--diff-out`], [`<path>`], [\u{2014}], [`--diff-mode` (또는 FLIP) 결과를 PNG 로 저장], [8장],
  [`--sbs`], [\u{2014}], [off], [`--diff-out` 을 `A|diff|B` 가로 3연결 합성으로 저장], [8장],
  [`--validate`], [\u{2014}], [off], [float/HDR 영상의 NaN/Inf/음수/`>1` 픽셀 스캔 (CSV). GUI 대응은 #key("/")], [8장],
  [`--probe`], [`<X,Y>`], [\u{2014}], [픽셀 X,Y 의 컬러메트리(XYZ/xy/u\u{2032}v\u{2032}/CCT/Duv/L\*) 를 CSV 로. B 가 있으면 B 행과 delta 행 추가], [9장],
  [`--uniformity`], [\u{2014}], [off], [평판 균일도: ICDM 9/13/25점 %, CV, 색 \u{0394}u\u{2032}v\u{2032}, SEMU 무라 프록시. A\[,B\] \u{2192} A/B/\u{0394} 행], [8장],
  [`--comp-out`], [`<file|->`], [\u{2014}], [comp 블록별 표를 파일 또는 stdout(`-`) 으로. `--comp` 필수], [5장, 8장],
  [`--comp-batch`], [\u{2014}], [off], [세 디렉토리의 같은 이름 프레임 전체를 프레임당 한 줄로 요약. `--comp` 필수], [5장, 8장],
)

=== CI 게이트 옵션

#table(
  columns: (auto, auto, auto, 1fr),
  [*옵션*], [*인자*], [*기본값*], [*FAIL 조건*],
  [`--fail-psnr`], [`<dB>`], [미설정(\u{2212}1)], [`psnr_db` \< 값],
  [`--fail-ssim`], [`<v>`], [미설정], [`ssim` \< 값],
  [`--fail-flip`], [`<v>`], [미설정], [`flip` \> 값],
  [`--fail-maxerr`], [`<v>`], [미설정], [`max_error` \> 값],
  [`--warn-psnr`], [`<dB>`], [미설정], [`psnr_db` \< 값 \u{2192} WARN 만. 종료 코드는 0 유지],
  [`--fail-uniformity`], [`<pct>`], [미설정], [`uni25_pct` \< 값],
  [`--fail-semu`], [`<v>`], [미설정], [`semu` \> 값],
)

게이트가 하나라도 설정되어 있고 FAIL 이 한 건이라도 있으면 프로세스는 *exit 10*
으로 끝납니다. 자세한 CI 연동 스크립트는 8장을 참조하십시오.

```
av --metrics --pair --fail-psnr 40 --warn-psnr 45 \
   /data/ddi/orig/f0001.png /data/ddi/hw > metrics.csv
```

=== 인자 검증 규칙

#table(
  columns: (auto, 1fr),
  [*규칙*], [*내용*],
  [위치 인자], [최대 3개. 4번째부터는 `Too many image arguments (max 3)` 로 exit 1],
  [`--comp`], [imageA/B/C 가 모두 있어야 합니다. 하나라도 없으면 exit 1],
  [세 번째 위치 인자], [`--comp` 를 쓰지 않아도 세 번째 경로가 주어지면 comp 모드가 자동으로 켜집니다],
  [`--comp-out` / `--comp-batch`], [`--comp` (이미지 3장) 없이 쓰면 exit 1],
  [`--pair`], [경로 2개 필요. 두 디렉토리가 같으면 "Nothing to compare" 로 exit 1],
  [알 수 없는 `--` 옵션], [`Unknown option:` 출력 후 exit 1],
  [값 누락], [`Expected value after <옵션>` 출력 후 exit 1],
)

== 종료 코드

#table(
  columns: (auto, auto, 1fr),
  [*코드*], [*의미*], [*발생 조건*],
  [`0`], [성공], [정상 종료. 게이트가 WARN 만 낸 경우, `--validate` 가 `negative`/`superwhite` 만 찾은 경우, 8비트 영상에 `--validate` 를 건 경우 포함],
  [`1`], [CLI 파싱\u{00B7}검증 실패], [알 수 없는 옵션, 잘못된 `--blk`/`--format`/`--blk-metric`/`--diff-mode`/hex 색, 값 누락, 위치 인자 4개 이상, `--comp` 이미지 부족, `--comp-out`/`--comp-batch` 를 `--comp` 없이 사용, `--pair` 두 디렉토리 동일],
  [`2`], [초기화 실패], [`SDL_Init` / `SDL_CreateWindow` / `SDL_CreateRenderer` / ImGui 백엔드 / `MainWindow::init()` 실패. GUI 경로에서만 발생],
  [`3`], [헤드리스 인자 오류], [이미지 인자 부족, `--probe` 좌표 형식 오류 또는 범위 밖, 디렉토리에 영상 없음, 매니페스트 열기 실패, 유효 쌍 0개, `--comp-out` 출력 파일 열기 실패, `--comp-batch` 유효 프레임 0개],
  [`4`], [이미지 디코드 실패], [파일 없음\u{00B7}지원하지 않는 포맷\u{00B7}손상. `--metrics`/`--diff-out`/`--validate`/`--probe`/`--uniformity`/`--comp-out` 공통],
  [`5`], [크기/포맷 불일치], [단일 쌍 `--metrics`, `--diff-out`, `--comp-out` 에서 A 와 B(또는 orig 와 img2/img3) 의 해상도\u{00B7}자료형이 다를 때. 시퀀스 실행에서는 해당 행만 `mismatch` 로 남고 종료 코드가 되지 않습니다],
  [`6`], [FLIP 계산 실패], [`--diff-out --diff-mode flip` 에서 FLIP 엔진이 실패],
  [`7`], [PNG 쓰기 실패], [`--diff-out` 의 출력 경로가 없거나 권한\u{00B7}디스크 문제],
  [`8`], [불량 픽셀 발견], [`--validate` 에서 NaN 또는 Inf 가 한 개 이상. `negative`/`superwhite` 만으로는 8 이 되지 않습니다],
  [`10`], [CI 게이트 FAIL], [`--fail-psnr`/`--fail-ssim`/`--fail-flip`/`--fail-maxerr`/`--fail-uniformity`/`--fail-semu` 중 하나 이상 위반],
)

#tip[`set -e` 를 쓰는 CI 스크립트에서는 exit 10 이 스크립트를 즉시 죽여 리포트
수집 단계가 실행되지 않습니다. 게이트 판정 명령만 `|| rc=$?` 로 감싸십시오 (8장).]

== CSV / JSON 스키마 요약

모든 헤드리스 CSV 는 첫 줄이 헤더이고, stdout 에는 데이터만 나갑니다. 사람이 읽는
요약 줄과 오류 메시지는 stderr 로 분리됩니다. 무한대(완전 동일)는 `inf`,
수치 형식은 `%.6g` 입니다.

=== `--metrics` / `--batch` \u{2014} 16열

#table(
  columns: (auto, auto, 1fr),
  [*\#*], [*열 이름*], [*의미*],
  [1], [`file`], [A 의 파일명(basename). `--batch` 에서 label 을 주면 그 label],
  [2], [`width`], [영상 폭(픽셀)],
  [3], [`height`], [영상 높이(픽셀)],
  [4], [`psnr_db`], [종합 PSNR(dB). R/G/B 중 유한한 채널들의 평균, 모두 동일하면 `inf`],
  [5], [`psnr_r`], [Red 채널 PSNR],
  [6], [`psnr_g`], [Green 채널 PSNR],
  [7], [`psnr_b`], [Blue 채널 PSNR],
  [8], [`psnr_y`], [Rec.709 루마 PSNR],
  [9], [`ssim`], [SSIM 점수 (0 \u{2013} 1)],
  [10], [`flip`], [FLIP 평균 오차 (0 \u{2013} 1, 작을수록 좋음)],
  [11], [`mse`], [RGB 3채널 평균 MSE],
  [12], [`mae`], [RGB 3채널 평균 MAE],
  [13], [`max_error`], [채널을 통틀어 가장 큰 절대 오차],
  [14], [`msigned`], [루마 가중 평균 부호 오차 (A \u{2212} B). 부호가 밝기 편향 방향],
  [15], [`psnr_cb`], [Rec.709 Cb 크로마 PSNR],
  [16], [`psnr_cr`], [Rec.709 Cr 크로마 PSNR],
)

행 상태가 정상이 아니면 `<name>,,,<tag>` 뒤에 빈 필드가 채워진 플레이스홀더 행이
나갑니다. `<tag>` 는 `missing`(짝 파일 없음), `decode_error`(디코드 실패),
`mismatch`(크기/포맷 불일치) 중 하나입니다.

#ex("헤드리스 지표 한 쌍 확인")[
```
av --metrics /data/ddi/orig/f0001.png /data/ddi/hw/f0001.png
# file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr
# f0001.png,1920,1080,42.1337,41.88,42.55,41.96,42.61,0.99312,0.0184,3.98,1.21,37,-0.164,48.3,47.1
```
`msigned` 가 음수이므로 보상 결과(B) 가 원본(A) 보다 전체적으로 밝습니다.
]

`--format json` 은 같은 값을 `frames[]` 배열과 `summary` 객체(프레임 수,
`psnr`/`ssim`/`flip` 의 mean/median/p95/min/max, 게이트 판정)로 내보내고,
`--format junit` 은 `<testsuite>` 아래 프레임당 `<testcase>` 를 만들며 FAIL 은
`<failure>`, 상태 행은 `<skipped>` 로 표시합니다.

=== `--probe` \u{2014} 11열

#table(
  columns: (auto, auto, 1fr),
  [*\#*], [*열 이름*], [*의미*],
  [1], [`which`], [`A` / `B` / `delta` 중 하나],
  [2], [`X`], [CIE XYZ 의 X],
  [3], [`Y`], [CIE XYZ 의 Y (휘도)],
  [4], [`Z`], [CIE XYZ 의 Z],
  [5], [`x`], [CIE 1931 색도 x],
  [6], [`y`], [CIE 1931 색도 y],
  [7], [`uprime`], [CIE 1976 u\u{2032}],
  [8], [`vprime`], [CIE 1976 v\u{2032}],
  [9], [`CCT`], [상관 색온도 (K). delta 행에서는 \u{0394}CCT],
  [10], [`Duv`], [Planckian locus 로부터의 거리. delta 행에서는 \u{0394}u\u{2032}v\u{2032}],
  [11], [`Lstar`], [CIE L\*. delta 행에서는 \u{0394}E76],
)

#note[`delta` 행은 2 \u{2013} 8 열이 비어 있고 9/10/11 열만 채워집니다. 즉 같은
열 번호라도 A/B 행과 delta 행의 *의미가 다릅니다*. 파싱 스크립트에서 반드시
`which` 열로 분기하십시오.]

=== `--uniformity` \u{2014} 11열

#table(
  columns: (auto, auto, 1fr),
  [*\#*], [*열 이름*], [*의미*],
  [1], [`file`], [파일명. \u{0394} 행에서는 `delta`],
  [2], [`role`], [`A` / `B` / `B-A`],
  [3], [`width`], [영상 폭],
  [4], [`height`], [영상 높이],
  [5], [`Lmean`], [화면 전체 평균 휘도(CIE Y)],
  [6], [`uni9_pct`], [ICDM 9점 휘도 균일도 % \u{2014} 격자점 휘도의 `100 * Lmin / Lmax`],
  [7], [`uni13_pct`], [ICDM 13점 균일도 % (3\u{00D7}3 \u{002B} 내부 4점)],
  [8], [`uni25_pct`], [ICDM 25점 균일도 %. `--fail-uniformity` 가 보는 값],
  [9], [`nonuni_cv_pct`], [전 픽셀 휘도의 변동계수 CV %],
  [10], [`duv_max`], [25점 각각의 u\u{2032}v\u{2032} 와 화면 평균 u\u{2032}v\u{2032} 사이 최대 \u{0394}u\u{2032}v\u{2032}],
  [11], [`semu`], [SEMU 무라 프록시. `--fail-semu` 가 보는 값],
)

계산 실패 행은 3열에 `error` 가 들어간 플레이스홀더가 됩니다. `B-A` 행은 각
항목의 단순 차이(B \u{2212} A)이므로 `uni*_pct` 는 \u{002B} 가, `cv`\u{00B7}`semu`
는 \u{2212} 가 "보상으로 좋아졌다" 는 뜻입니다.

=== `--comp-out` \u{2014} 11열 (블록 단위)

#table(
  columns: (auto, auto, 1fr),
  [*\#*], [*열 이름*], [*의미*],
  [1], [`bx`], [블록 열 인덱스 (0 시작)],
  [2], [`by`], [블록 행 인덱스 (0 시작)],
  [3], [`x`], [블록 좌상단 픽셀 X. `bx * blk_w`],
  [4], [`y`], [블록 좌상단 픽셀 Y. `by * blk_h`],
  [5], [`w`], [블록 실제 폭 (오른쪽 끝은 잘릴 수 있음)],
  [6], [`h`], [블록 실제 높이],
  [7], [`mse_img2`], [orig 대비 img2 의 블록 MSE],
  [8], [`psnr_img2`], [orig 대비 img2 의 블록 PSNR(dB). 완전 동일이면 `inf`],
  [9], [`mse_img3`], [orig 대비 img3 의 블록 MSE],
  [10], [`psnr_img3`], [orig 대비 img3 의 블록 PSNR(dB)],
  [11], [`dpsnr`], [`psnr_img2 - psnr_img3`. \u{002B} 면 img2 우세, 한쪽만 `inf` 면 `inf`/`-inf`],
)

행은 worst 목록이 아니라 *영상의 모든 블록* 입니다 (`nbx * nby` 줄).
stderr 로 블록 수, 0 이 아닌 블록 수, 전역 PSNR, `|d| > 1dB` 기준 승패 집계가
한 줄 나갑니다.

```
av --comp --comp-out - --blk 16x16 --num_blk 24 --blk-metric y \
   orig/f0001.png fw/f0001.png hw/f0001.png | head -3
```

=== `--comp-batch` \u{2014} 10열 (프레임 단위)

#table(
  columns: (auto, auto, 1fr),
  [*\#*], [*열 이름*], [*의미*],
  [1], [`file`], [프레임 파일명 (orig 디렉토리 기준)],
  [2], [`psnr_img2`], [프레임 전역 PSNR (orig 대비 img2)],
  [3], [`psnr_img3`], [프레임 전역 PSNR (orig 대비 img3)],
  [4], [`dpsnr`], [`psnr_img2 - psnr_img3`],
  [5], [`win2`], [img2 가 이긴 블록 수 (`d > 1dB`)],
  [6], [`win3`], [img3 가 이긴 블록 수 (`d < -1dB`)],
  [7], [`tie`], [무승부 블록 수 (`abs(d) <= 1dB`)],
  [8], [`worst2`], [img2 의 \#1 worst 블록 `bx:by@psnr`],
  [9], [`worst3`], [img3 의 \#1 worst 블록 `bx:by@psnr`],
  [10], [`status`], [`ok` / `missing`(짝 프레임 없음) / `mismatch`(크기 불일치)],
)

=== `--validate` \u{2014} 2열

#table(
  columns: (auto, 1fr),
  [*열 이름*], [*의미*],
  [`class`], [`nan` / `inf` / `negative` / `superwhite` 네 행이 항상 이 순서로 나옵니다],
  [`count`], [해당 분류에 속한 채널값 개수 (RGB 3채널을 각각 셈)],
)

값이 하나라도 있으면 그 아래에 `# first offenders: class,x,y,channel` 주석과
최대 20개의 위치가 `#` 주석으로 따라붙습니다. 주석 줄이므로 CSV 파서에서
`#` 로 시작하는 줄만 건너뛰면 됩니다.

== 지원 파일 포맷

디렉토리 시퀀스 스캔과 파일 열기 대화상자가 인식하는 확장자는 다음 8종입니다
(`SUPPORTED_IMG_EXTS`, 대소문자 무시).

```
.png  .jpg  .jpeg  .bmp  .tga  .pgm  .ppm  .hdr
```

#table(
  columns: (auto, auto, auto, 1fr),
  [*포맷*], [*확장자*], [*내부 표현*], [*비고*],
  [PNG], [`.png`], [RGBA8], [8/16비트, 알파 지원. 저장\u{00B7}클립보드 복사의 기본 포맷],
  [JPEG], [`.jpg` `.jpeg`], [RGBA8], [손실 압축. 압축 아티팩트가 지표에 그대로 잡히므로 회귀 기준 영상으로는 권장하지 않습니다],
  [BMP], [`.bmp`], [RGBA8], [무손실. 저장 포맷으로도 선택 가능],
  [TGA], [`.tga`], [RGBA8], [알파 채널 포함],
  [PGM], [`.pgm`], [원본 정수 \u{002B} RGBA8], [P2(ASCII) / P5(binary) 그레이스케일. `maxval` 보존],
  [PPM], [`.ppm`], [원본 정수 \u{002B} RGBA8], [P3(ASCII) / P6(binary) 컬러. `maxval` 이 255 가 아닌 10/12비트 파일도 *원본값 그대로* 유지 (최대 65535)],
  [HDR], [`.hdr`], [RGBA32F], [Radiance RGBE. float 로 유지되어 `--validate` 와 #key("/") 오버레이의 대상이 됩니다],
)

#note[PNM 계열은 av 자체 파서가 먼저 시도하고(P2/P3/P5/P6 매직 넘버 확인), 실패하면
`stb_image` 로 넘어갑니다. 그래서 10비트 PPM 의 원본값이 8비트로 눌리지 않고
픽셀 풍선말\u{00B7}통계\u{00B7}라인컷에 그대로 나타납니다 (3장). 저장 시에도
`P6 / 10bit` 를 고르면 `maxval 1023` 인 PPM 이 나옵니다 (7장).]

#warn[헤드리스 경로(`src/metrics_cli.cpp`)는 GUI 와 달리 `stb_image` 만 사용합니다.
즉 `--metrics` 등에서 PPM 은 stb 의 PNM 디코더를 타므로 8비트로 정규화된 값이
쓰입니다. 10비트 원본값 자체를 다뤄야 하는 검증은 GUI 경로나 PNG 16비트를
쓰십시오.]

== 용어집

=== 지표 용어

#table(
  columns: (auto, 1fr),
  [*용어*], [*정의*],
  [PSNR], [Peak Signal-to-Noise Ratio. `20 log10(peak) - 10 log10(MSE)` (dB). `peak` 는 8비트 영상 255, float 영상 1.0. 값이 클수록 원본에 가깝고, 완전히 같으면 무한대(`inf`)입니다. av 의 종합 PSNR 은 R/G/B 채널 PSNR 중 유한하고 999 미만인 값들의 평균입니다],
  [SSIM], [Structural Similarity Index. 국소 창에서 밝기\u{00B7}대비\u{00B7}구조 세 요소를 비교해 0 \u{2013} 1 로 냅니다. 1 이 완전 일치. 블러\u{00B7}구조 왜곡처럼 PSNR 이 잘 못 잡는 결함에 민감합니다],
  [FLIP], [NVIDIA 의 지각 오차 지표. 사람이 두 영상을 번갈아 볼 때 느끼는 차이를 0(동일) \u{2013} 1 로 모델링합니다. av 는 magma 색맵으로 오차맵을 그리고 평균값을 `flip` 열에 씁니다. 값이 *작을수록* 좋으므로 게이트도 "초과 시 FAIL" 입니다],
  [MSE], [Mean Squared Error. 픽셀 차이의 제곱 평균. av 의 `mse` 열은 R/G/B 세 채널 MSE 의 평균입니다. 큰 오차에 가중이 크게 실립니다],
  [MAE], [Mean Absolute Error. 픽셀 차이 절대값의 평균. MSE 보다 이상치에 둔감해서 "전반적으로 얼마나 어긋나 있는가" 를 봅니다],
  [msigned], [루마 가중 평균 부호 오차. 채널별 평균 부호 오차(A \u{2212} B)에 Rec.709 루마 계수 `0.2126` / `0.7152` / `0.0722` 을 곱해 더한 값입니다. 0 에 가까우면 편향 없음, 음수면 B(보상 결과)가 전체적으로 밝고 양수면 어둡습니다. PSNR/MSE 가 버리는 *방향 정보* 를 담습니다],
)

=== 색 용어

#table(
  columns: (auto, 1fr),
  [*용어*], [*정의*],
  [\u{0394}u\u{2032}v\u{2032}], [CIE 1976 UCS 색도 평면에서 두 색 사이의 유클리드 거리. 지각적으로 균등한 척도라서 색 편차 판정에 씁니다. 통상 0.004 이하면 나란히 놓고도 구분하기 어렵다고 봅니다],
  [CCT], [Correlated Color Temperature. 상관 색온도(K). av 는 McCamy 1992 근사식을 CIE 1931 xy 에 적용합니다. 백색점이 "따뜻한가/차가운가" 를 한 숫자로 요약합니다],
  [Duv], [Planckian locus(흑체 궤적)로부터의 부호 있는 거리. CIE 1960 uv 로 변환한 뒤 6차 다항 근사로 계산합니다. \u{002B} 면 궤적 위쪽(초록 기미), \u{2212} 면 아래쪽(자홍 기미)입니다. CCT 만으로는 알 수 없는 백색점의 색 틀어짐을 잡습니다],
  [SEMU], [무라의 *가시성* 을 정규화한 지표. av 는 배경면을 뺀 잔차의 최악 Weber 대비 `Cmax` 를 결함 면적 `S`(화면 대비 %)의 가시성 계수로 나눕니다 \u{2014} `semu = 100 * Cmax / (1.97 / S^0.33 + 0.72)`. 평탄한 화면은 0, 국소 얼룩은 대비\u{00B7}면적에 따라 커집니다. 화소 피치와 시청 거리를 모델링하지 않은 *프록시* 이므로 절대 합격선보다 *같은 조건에서의 상대 비교* 에 쓰십시오],
  [ICDM], [Information Display Measurements Standard. 디스플레이 측정 규격으로, 평판 균일도를 화면상의 9점\u{00B7}13점\u{00B7}25점 격자에서 측정하도록 정의합니다. av 의 `uni9_pct`/`uni13_pct`/`uni25_pct` 가 이 격자를 따릅니다],
  [LUT], [Look-Up Table. 입력 색을 표에서 찾아 출력 색으로 바꾸는 변환입니다. av 는 `.cube` 형식의 3D/1D LUT 를 `--lut` 로 받아 *표시에만* 적용합니다 (#key("'") 토글). 지표 계산은 룩을 적용하기 전 원시 픽셀로 합니다],
  [CDL], [ASC Color Decision List. slope / offset / power 세 파라미터에 채도(sat) 를 더한 표준 색 보정 기술입니다. av 는 `.cdl` / `.ccc` 파일을 `--cdl` 로 읽어 LUT 와 같은 표시 전용 경로에 적용합니다],
)

=== `--comp` 와 실무 용어

#table(
  columns: (auto, 1fr),
  [*용어*], [*정의*],
  [spike 픽], [`--comp` 의 worst 블록 목록 중 *블록 내 최대 픽셀 오차* 기준으로 뽑힌 항목입니다. 목록의 위쪽 3/4 은 지표 MSE 순, 나머지 1/4 이 이 방식입니다. 화면에서는 마젠타 색 박스와 `P` 표시, 리스트 창에서는 순번 옆 `P` 로 구분합니다. 63픽셀이 정확하고 1픽셀만 크게 튀는 점 결함처럼 MSE 에 묻히는 결함을 잡기 위한 장치입니다 (5장)],
  [win/loss], [블록별로 `dpsnr = psnr_img2 - psnr_img3` 를 계산해 `> 1dB` 면 img2 승, `< -1dB` 면 img3 승, 그 사이는 무승부로 집계한 것입니다. 두 알고리즘(예: FW 보상 대 HW 보상) 중 어느 쪽이 화면의 어느 영역에서 유리한지 보는 지도이며, #key("Ctrl+W") 맵과 `--comp-batch` 의 `win2`/`win3`/`tie` 열로 나타납니다],
  [DDI], [Display Driver IC. 패널을 구동하는 드라이버 칩입니다. 이 매뉴얼의 주 사용 맥락은 DDI 안의 보상 IP(디무라\u{00B7}감마\u{00B7}색보정 등)가 만든 출력 프레임을 원본과 비교해 화질 회귀를 검증하는 작업입니다],
  [무라 (mura)], [패널 표면에 나타나는 얼룩\u{00B7}국소 휘도 또는 색 불균일을 가리키는 용어입니다. 전체적인 밝기 기울기(shading) 와 달리 국소적이며, 사람 눈에 "얼룩" 으로 인지됩니다. av 의 `--uniformity` 는 2차 다항 배경면을 최소자승으로 제거해 shading 을 걷어낸 뒤 남은 잔차로 SEMU 를 계산하므로, 순수한 기울기는 0 에 가깝게, 국소 얼룩은 값이 살아남게 설계돼 있습니다],
  [ROI], [Region of Interest. 관심 영역입니다. #key("Ctrl+E") 로 모드를 켜고 좌드래그로 사각형을 지정하면 그 영역만의 통계와 컬러메트리를 볼 수 있습니다 (6장)],
)
