#import "_common.typ": *

= av 소개와 첫 실행

이 장에서는 av가 어떤 도구이고 어디에 설치되어 있으며, 명령 한 줄로 어떻게
띄우는지를 다룹니다. 실행 형태 네 가지(인자 없음 / 1장 / 2장 / 3장), 시작
옵션 전체, 그리고 처음 화면에 보이는 것들(메뉴바·패널·상태바·파일명 토스트)과
종료 방법까지 익히면 나머지 장을 읽을 준비가 끝납니다.

== av는 어떤 도구인가

av는 "Advanced Pixel Lens"의 줄임말로, 이미지를 *픽셀 단위로 파고들어 보는
뷰어*이자 두(또는 세) 장의 이미지를 나란히 놓고 차이를 계량하는 *비교기*입니다.
창 제목과 앱 아이콘에도 `Advanced Pixel Lens`라는 이름이 그대로 쓰입니다.

#table(
  columns: (auto, 1fr),
  [*항목*], [*내용*],
  [언어],     [C++20],
  [윈도우/입력], [SDL3 (`SDL_CreateWindow`, 스캔코드 기반 키 처리)],
  [UI],       [Dear ImGui (메뉴바·상태바·분석 창·풍선말 전부 ImGui)],
  [렌더링],   [OpenGL 3.3 Core (GLSL `#version 150`). 실패하거나 `--software` 지정 시 SDL 소프트웨어 렌더러로 자동 폴백],
  [설정 파일], [`~/.av.ini` (종료 시 자동 저장)],
)

주 용도는 두 가지입니다.

+ *DDI(디스플레이 드라이버 IC) 보상 IP 검증* \u{2014} 보상 전 원본과 보상 IP가 뱉은
  결과를 A/B로 띄워 놓고, PSNR·SSIM·FLIP 같은 지표와 블록 단위 최악 구간을 찾아
  화질 열화가 어디서 발생했는지 짚어냅니다. 원본 대비 FW 결과와 HW 결과를 한
  화면에서 셋으로 비교하는 `--comp` 모드(5장 참조)가 이 목적에 특화된 기능입니다.
+ *일반 영상 알고리즘 A/B 비교* \u{2014} 스케일러, 노이즈 리덕션, 톤매핑 등 임의의
  두 결과물을 비교하는 범용 도구로도 그대로 씁니다.

=== GUI 모드와 헤드리스 모드

av는 하나의 실행 파일이지만 실행 방식은 두 갈래입니다. 이 장부터 7장까지는
GUI 모드를, 8장은 헤드리스 모드를 다룹니다.

#table(
  columns: (auto, auto, 1fr),
  [*모드*], [*트리거*], [*동작*],
  [GUI],      [헤드리스 플래그가 없을 때],
              [창을 띄우고 이벤트 루프를 돈다. 사람이 눈으로 보고 키로 조작],
  [헤드리스],  [`--metrics` `--batch` `--comp-out` `--comp-batch` `--diff-out` `--validate` `--probe` `--uniformity` 중 하나],
              [SDL·OpenGL·창을 아예 만들지 않고 계산 결과(CSV/JSON/JUnit 또는 PNG)만 내보낸 뒤 즉시 종료. SSH·CI 파이프라인용],
)

#note[헤드리스 분기는 `main()`의 맨 앞, `parse_cli()` 직후에 일어납니다. 즉
헤드리스 플래그가 하나라도 있으면 그래픽 드라이버가 없는 서버에서도 안전하게
돕니다. 자세한 컬럼 규격과 CI 게이트(exit 10 등)는 8장을 보십시오.]

== 설치 위치와 실행 방법

세 플랫폼 모두 `PATH`에 잡히는 위치에 설치되어 있으므로, 어느 디렉토리에서든
`av`만 치면 실행됩니다.

#table(
  columns: (auto, auto, 1fr),
  [*플랫폼*], [*설치 위치*], [*실행 예*],
  [macOS],   [`~/.local/bin/av`],
             [`av a.png b.png`],
  [Linux (alexws)], [번들 디렉토리 설치 (`av` 래퍼 + `av.bin` + `lib/`)],
             [`av a.png b.png`],
  [Windows], [`C:\Windows\av.exe`],
             [`av a.png b.png`],
)

#note[Linux 번들은 실행 파일과 의존 라이브러리를 한 디렉토리에 묶어 배포하는
형태입니다. 실제로 실행되는 것은 래퍼 스크립트 `av`이고, 그 안에서 번들된
`ld-linux`와 `lib/`를 지정해 `av.bin`을 띄웁니다. 오프라인 서버에 tar로 풀어
놓기만 해도 동작합니다.]

#tip[SSH로 접속한 Linux에서 GUI를 띄우면 av가 `SSH_CONNECTION` 환경변수를 보고
자동으로 소프트웨어 렌더러로 전환하며 아래 한 줄을 출력합니다. `--software`를
손으로 붙일 필요가 없습니다.

```
[av] X11 forwarding detected (SSH) — using software renderer
```
]

== 버전과 도움말 확인

=== 버전 \u{2014} `--version`

```
av --version
```

출력은 다음 형식입니다. `AV_VERSION_FULL`은 `git describe` 기반이고 날짜는 최신
커밋 날짜입니다.

```
av v0.22-80-g096f41f  (updated 2026-07-31)
```

버그를 보고하거나 CI 로그를 남길 때는 이 한 줄을 그대로 첨부하십시오. 태그
뒤의 `-80-g096f41f`는 "태그 이후 80개 커밋, 해시 096f41f"라는 뜻이라 빌드를
정확히 특정할 수 있습니다.

=== 도움말 \u{2014} `-h` / `--help`

```
av --help
```

앞부분은 다음과 같습니다(전체는 40행 남짓이며, 뒤쪽은 5장·8장·9장에서 다루는
옵션들입니다).

```
Usage: av [options] [imageA] [imageB]

Options:
  --diff-mode <mode>   none|alphablend|abs|rel|highlight|falsecolor|ssim|flip|signed|enhance  (default: none)
  --zoom <factor>      fit|0|1|2 etc. (0/fit=window, 1=1:1 actual; saved)
  --sync               Enable viewport sync          (default: on)
  --no-sync            Disable viewport sync
  ...
  --amplify <val>      Diff amplification 0.1-100   (default: 1.0)
  --fullscreen         Start in fullscreen
  --geometry <WxH>     Initial window size           (default: 1280x720)
  -p, --pan-step <N>   Shift+hjkl jump size in pixels   (default: 32)
  -bc <A> <B> <D>      Border colours for A/B/Diff panels as 6-digit hex
                       e.g. -bc ff00ff ffff00 00ffff   (default: magenta/yellow/cyan)
  -d, --diff           Show pixel-absolute diff (shortcut)
  --software           Force SDL software renderer (no OpenGL)
  --windowed           Start in windowed mode (title bar + resizable)
  -nb                  Start with panel borders hidden
  --version            Print version and exit
  -h, --help           Print this help
```

`-h`와 `--help`, `--version`은 모두 *출력 후 즉시 종료(exit 0)*합니다. 창이
뜨지 않으므로 스크립트에서 안전하게 호출할 수 있습니다.

#warn[알 수 없는 옵션(`--`로 시작하는 미지의 토큰)을 주면 av는
`Unknown option: ...`을 stderr로 찍고 *exit 1*로 죽습니다. 오타가 조용히 무시되지
않으므로, CI 스크립트에서 옵션 철자를 바꿀 때 안심해도 됩니다.]

== 네 가지 기본 실행 형태

위치 인자(파일 경로)는 최대 3개까지 받으며, 순서대로 imageA, imageB, imageC로
해석됩니다. 몇 개를 주느냐에 따라 레이아웃이 자동으로 결정됩니다.

#fig_panel_modes()

#table(
  columns: (auto, auto, 1fr),
  [*형태*], [*명령*], [*화면*],
  [인자 없음], [`av`],
    [빈 뷰어. 메뉴 File \u{2192} Open Image A… 또는 #key("Shift+Ctrl+O")로 여는다. 창에 파일을 드래그 앤 드롭해도 로드됨],
  [1장], [`av result.png`],
    [단일 패널이 창 전체를 차지],
  [2장 (A,B 비교)], [`av orig.png comp.png`],
    [좌우 반반 A \| B. diff 모드를 켜면 A \| B \| Diff 3분할로 바뀜],
  [3장 (`--comp`)], [`av --comp orig.png fw.png hw.png`],
    [3분할 orig \| img2 \| img3. diff는 강제로 꺼짐],
)

=== 인자 없이 실행

```
av
```

빈 화면에서 시작합니다. 파일을 여는 방법은 세 가지입니다.

- #key("Shift+Ctrl+O") (macOS는 #key("Shift+Cmd+O")) \u{2014} 현재 활성 패널에 열기
- 메뉴바 File \u{2192} `Open Image A…` / `Open Image B…` \u{2014} 패널을 지정해서 열기
- 탐색기에서 창으로 *드래그 앤 드롭* \u{2014} A가 비어 있으면 A에, 차 있으면 B에 로드

=== 1장 열기

```
av /data/ddi/compensated/frame_0001.png
```

한 장만 주면 패널 하나가 창 전체를 씁니다. 이 상태에서도 #key(";") / #key("A")로
같은 디렉토리의 다음/이전 이미지를 순회할 수 있습니다(7장 참조).

=== 2장 비교 (A = 기준, B = 비교)

```
av /data/ddi/orig/frame_0001.png /data/ddi/comp/frame_0001.png
```

*왼쪽(A)이 기준, 오른쪽(B)이 비교 대상*입니다. PSNR·SSIM·FLIP을 비롯한 모든
지표는 "A 대비 B"로 계산되므로, DDI 검증에서는 반드시 원본을 A에, 보상 IP
출력을 B에 놓으십시오. 순서를 잘못 넣었다면 #key("Shift+Space")로 A와 B를
맞바꿀 수 있습니다.

=== 3장 비교 (`--comp`)

```
av --comp /data/ddi/orig/f001.png /data/ddi/fw/f001.png /data/ddi/hw/f001.png
```

원본 하나와 비교 대상 둘(예: FW 모델 결과와 HW RTL 결과)을 한 화면에 놓고,
각 블록별로 어느 쪽이 원본에 더 가까운지 판정하는 전용 모드입니다. 이 모드에서는
diff가 강제로 비활성화되며, 시작할 때 `[comp]`로 시작하는 요약이 stdout에
출력됩니다. 자세한 사용법(블록 크기, worst 블록 순회, 승패 맵 등)은 5장을 보십시오.

#note[세 번째 위치 인자를 주면 `--comp`를 생략해도 자동으로 3영상 모드가 켜집니다.
반대로 `--comp`를 줬는데 이미지가 3개가 아니면
`--comp needs three images: av --comp <orig> <img2> <img3>` 를 찍고 exit 1로 종료합니다.]

== 시작 옵션 전수

=== 초기 뷰 상태 \u{2014} `--zoom`, `--sync` / `--no-sync`

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*기본값*], [*설명*],
  [`--zoom <factor>`], [`fit` (= 0)],
    [초기 배율. `fit` 또는 `0`이면 창에 맞춤, `1`이면 1:1 실제 크기, `2`면 2배 \u{2026}. 값은 `~/.av.ini`의 `zoom=` 키에 *영속화*되며, 다음 실행에서 `--zoom`을 생략하면 저장값이 그대로 재현됨. 배율은 `0.125`\~`256`으로 클램프됨],
  [`--sync`], [on],
    [A/B 패널의 줌·팬을 잠금 동기화. GUI에서는 #key("S")로 토글],
  [`--no-sync`], [\u{2014}],
    [동기화를 끄고 시작. 해상도가 다른 두 영상을 각각 따로 보고 싶을 때],
)

```
av --zoom 4 orig.png comp.png      # 4배로 시작 (이후 실행에도 4배가 유지됨)
av --zoom fit orig.png comp.png    # 창 맞춤으로 되돌리기
av --no-sync orig.png comp.png     # A/B를 따로 움직이기
```

#warn[`--zoom`은 *영속*되지만 `--sync`/`--no-sync`는 CLI 값이 항상 이깁니다.
`~/.av.ini`에 `sync_viewports=0`이 저장돼 있어도, `--no-sync` 없이 실행하면
기본값 on이 덮어씁니다. 동기화를 끈 상태로 시작하고 싶다면 매번 `--no-sync`를
붙이십시오.]

#tip[DDI 보상 결과를 볼 때는 `--zoom 8` 정도로 시작해 두면 매 프레임 확대
조작을 반복하지 않아도 됩니다. `--zoom`으로 지정한 배율은 #key(";") / #key("A")로
다음 영상을 넘길 때도 유지되므로 시퀀스를 훑을 때 화면이 튀지 않습니다.]

=== 창과 렌더러 \u{2014} `--fullscreen`, `--geometry`, `--windowed`, `--software`

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*기본값*], [*설명*],
  [`--fullscreen`], [off],
    [`SDL_WINDOW_FULLSCREEN`으로 시작. 프로젝터·검증용 모니터에 꽉 채워 띄울 때],
  [`--geometry <WxH>`], [`1280x720`],
    [초기 창 크기(픽셀). `x` 또는 `X` 구분자 모두 허용],
  [`--windowed`], [off],
    [타이틀바가 있는 일반 창 모드로 시작. 지정하지 않으면 *테두리 없는 최대화 창*이 기본],
  [`--software`], [off],
    [OpenGL을 쓰지 않고 SDL 소프트웨어 렌더러 강제. 원격 데스크톱·가상머신·GL 드라이버 문제 시],
)

```
av --geometry 1920x1080 --windowed orig.png comp.png
av --fullscreen --comp orig.png fw.png hw.png
av --software orig.png comp.png      # GL이 말썽일 때
```

#warn[기본(=`--windowed` 미지정)은 `SDL_WINDOW_BORDERLESS`에
`SDL_WINDOW_MAXIMIZED`가 함께 붙습니다. 즉 창이 곧바로 최대화되므로
`--geometry`로 준 크기가 눈에 보이지 않을 수 있습니다. `--geometry`를 확실히
적용하려면 `--windowed`와 함께 쓰십시오.]

창 모드는 GUI에서 #key("W")로 언제든 토글할 수 있고, 그 상태는 `~/.av.ini`의
`windowed_mode=`에 저장됩니다. 타이틀바에 파일명이 표시되는 것은 `--windowed`
모드일 때뿐이며, 형식은 다음과 같습니다 (대괄호 안은 현재 diff 모드 태그).

```
av — f001.png | f001_comp.png [Abs]
```

테두리 없는 기본 모드에서는 제목이 `Advanced Pixel Lens`로 고정됩니다.

=== 패널 외형 \u{2014} `-nb`, `-bc`

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*기본값*], [*설명*],
  [`-nb`], [테두리 표시],
    [패널 테두리를 숨긴 상태로 시작. GUI에서 #key("B")로 토글되며 이 토글값은 `~/.av.ini`의 `show_borders=`에 *영속화*됨],
  [`-bc <A> <B> <D>`], [magenta / yellow / cyan],
    [A·B·Diff 패널 테두리 색을 6자리 hex로 지정. 값 3개를 *반드시 모두* 줘야 함. 영속화되지 않으므로 매번 지정],
)

```
av -bc ff00ff ffff00 00ffff orig.png comp.png   # 기본값과 동일
av -bc 00ff00 ff8000 ffffff orig.png comp.png   # 초록 / 주황 / 흰색
av -nb orig.png comp.png                        # 테두리 없이 시작
```

테두리는 알파 230으로 그려지며 A(왼쪽)=마젠타 `ff00ff`, B(오른쪽)=노랑 `ffff00`,
Diff=시안 `00ffff`가 기본입니다. `--comp` 3분할에서도 같은 세 색이 orig / img2 /
img3 순으로 재사용됩니다. hex 파싱에 실패하면 `Invalid hex colour: ...`를 찍고
exit 1로 종료합니다.

#tip[캡처해서 보고서에 넣을 때는 `-nb`로 테두리를 지우면 이미지 경계가 깔끔하게
잘립니다. 반대로 화면 캡처를 여러 장 이어 붙일 때는 색 테두리가 어느 패널인지
구분해 주므로 켜 두는 편이 낫습니다.]

=== 키보드 이동 폭 \u{2014} `-p` / `--pan-step`

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*기본값*], [*설명*],
  [`-p <N>`, `--pan-step <N>`], [`32`],
    [#key("Shift") + #key("h") #key("j") #key("k") #key("l") (또는 #key("Shift")+방향키) 로 이동할 때의 점프 폭(이미지 픽셀). #key("Shift") 없이 누르면 항상 1픽셀. 영속화되지 않음],
)

```
av -p 64 orig.png comp.png        # 한 번에 64픽셀씩 이동
av --pan-step 8 orig.png comp.png # 미세 이동 (8x8 블록 단위 검토용)
```

#tip[DDI 보상 IP가 8x8 또는 16x16 블록 단위로 동작한다면 `--pan-step`을 그 블록
크기와 맞춰 두십시오. #key("Shift+l")을 누를 때마다 정확히 블록 하나씩
넘어가므로, 블록 경계 아티팩트를 한 칸씩 훑기 좋습니다.]

=== diff 시작 상태 \u{2014} `-d`, `--diff-mode`, `--amplify`

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*기본값*], [*설명*],
  [`-d`, `--diff`], [off],
    [`--diff-mode abs`와 동일한 단축 옵션. 픽셀 절대차 diff로 바로 시작],
  [`--diff-mode <mode>`], [`none`],
    [시작 diff 모드 지정. 아래 10개 토큰 중 하나 (`none` = 끔, 실제 모드는 9종)],
  [`--amplify <val>`], [`1.0`],
    [diff 증폭 배율. 도움말상 범위는 `0.1`\~`100`. GUI에서 #key("[") / #key("]")로 조절할 때는 `0.5`\~`100`으로 클램프되고 #key("\\")로 1.0 리셋],
)

`--diff-mode`가 받는 토큰은 다음과 같습니다.

#table(
  columns: (auto, auto, 1fr),
  [*토큰*], [*단축키*], [*의미*],
  [`none`],       [#key("Ctrl+D")], [diff 끔 (기본)],
  [`alphablend`], [#key("Ctrl+2")], [A와 B를 알파 블렌딩],
  [`abs`],        [#key("Ctrl+3")], [픽셀 절대차 \|A\u{2212}B\|],
  [`rel`],        [#key("Ctrl+4")], [상대차],
  [`enhance`],    [#key("Ctrl+5")], [\[min,max\] \u{2192} \[128,255\] 리맵],
  [`falsecolor`], [#key("Ctrl+6")], [의사색 오차맵],
  [`ssim`],       [#key("Ctrl+7")], [SSIM 구조 유사도 히트맵],
  [`flip`],       [#key("Ctrl+0")], [FLIP 지각 오차맵 (magma)],
  [`signed`],     [#key("Ctrl+1")], [부호 있는 차 (파랑 = A\u{003C}B, 빨강 = A\u{003E}B)],
  [`highlight`],  [#key("Ctrl+9")], [임계 초과 픽셀 강조],
)

```
av -d orig.png comp.png                                  # abs diff로 바로 시작
av --diff-mode signed --amplify 8 orig.png comp.png      # 부호차를 8배 증폭
av --diff-mode flip orig.png comp.png                    # 지각 오차맵으로 시작
```

#ex("보상 IP의 미세 편향을 첫 화면에서 잡아내기")[
DDI 보상 결과는 원본 대비 차이가 1\~2 LSB에 그치는 경우가 많아 절대차를 그대로
보면 새까맣게 보입니다. 이때 `signed` 모드에 증폭을 걸어 시작하면 화면을 열자마자
편향의 *방향*이 드러납니다.

```
av --diff-mode signed --amplify 20 orig/f001.png comp/f001.png
```

패널 오른쪽 Diff 창이 전반적으로 푸르면 보상 결과가 원본보다 밝고(A\u{003C}B),
붉으면 어둡다(A\u{003E}B)는 뜻입니다. 회색이면 차이가 0입니다.
자세한 모드별 해석은 4장을 보십시오.
]

=== 다른 장에서 다루는 시작 옵션

아래 옵션들도 시작할 때 지정하지만, 기능 설명이 긴 만큼 해당 장에서 다룹니다.

#table(
  columns: (auto, 1fr),
  [*옵션*], [*참조*],
  [`--blk` `--num_blk` `--blk-metric` `--grid`], [5장 (세 영상 블록 비교)],
  [`--pair`], [7장 (이미지 시퀀스와 파일 입출력)],
  [`--metrics` `--format` `--fail-psnr` `--warn-psnr` `--fail-ssim` `--fail-flip` `--fail-maxerr` `--batch` `--comp-out` `--comp-batch` `--diff-out` `--sbs` `--validate` `--probe` `--uniformity` `--fail-uniformity` `--fail-semu`], [8장 (헤드리스 CLI와 CI 연동)],
  [`--profile` `--lut` `--cdl` `--no-color-mgmt`], [9장 (색 관리와 룩)],
)

== 화면 구성

#fig_ui_layout()

기본 상태에서는 이미지가 화면 전체를 차지하고 *메뉴바와 상태바는 숨겨져
있습니다*. #key("U")를 누르면 위쪽에 메뉴바, 아래쪽에 24픽셀 높이의 상태바가
나타납니다. 이 표시 여부는 `~/.av.ini`의 `show_ui=`에 저장되므로, 한 번 켜 두면
다음 실행에도 유지됩니다. 메뉴바 오른쪽 끝에는 항상
`NN fps  |  U: hide UI` 가 표시되어 다시 숨기는 방법을 알려 줍니다.

=== 메뉴바 \u{2014} File / View / Diff

File 메뉴:

#table(
  columns: (auto, auto, 1fr),
  [*항목*], [*단축키*], [*동작*],
  [`Open Image A…`], [\u{2014}], [A 패널에 파일 열기],
  [`Open Image B…`], [\u{2014}], [B 패널에 파일 열기],
  [`Open Image…`], [#key("Shift+Cmd+O")], [현재 활성 패널에 열기],
  [`Save Images…`], [#key("Shift+Cmd+S")], [A/B/Diff 저장 다이얼로그 토글 (7장)],
  [`Quit`], [#key("Q")], [종료],
)

View 메뉴 (표시·분석 창 토글이 모여 있습니다):

#table(
  columns: (auto, auto, 1fr),
  [*항목*], [*단축키*], [*참조*],
  [`Fit to Window`], [\u{2014}], [로드된 패널을 창 맞춤으로. 2장],
  [`1:1 Pixel`], [#key("Space")], [1:1 배율 + 중앙 정렬. 2장],
  [`Sync Viewports`], [#key("S")], [A/B 줌·팬 동기화 토글. 2장],
  [`Pathfinder: Image`], [#key("P")], [미니맵 표시. 2장],
  [`Pathfinder: Schematic`], [#key("Ctrl+P")], [도식형 미니맵. 2장],
  [`Show Image Info`], [#key("I")], [영상 정보 + PSNR 창. 3장],
  [`Show Pixel Info`], [#key("V")], [커서 픽셀값 풍선말. 3장],
  [`Colorimetry Probe (xy/CCT)`], [#key("Shift+V")], [CIE xy·u\u{2032}v\u{2032}·CCT 병기. 3장],
  [`Show Histogram`], [#key("Ctrl+H")], [히스토그램. 6장],
  [`Show H-Line Cut`], [#key("Ctrl+L")], [수평 절단면. 6장],
  [`Show V-Line Cut`], [#key("Ctrl+Y")], [수직 절단면. 6장],
  [`Show Statistics`], [#key("Ctrl+S")], [통계 창. 6장],
  [`ROI Stats`], [#key("Ctrl+E")], [ROI 선택 + 통계. 6장],
  [`Scatter Plot`], [#key("Ctrl+T")], [A vs B 산점도. 6장],
  [`Overlay/Blend`], [#key("O")], [겹쳐 보기. 하위에 Blend/Curtain 모드와 알파 슬라이더. 4장],
  [`Show Borders`], [#key("B")], [패널 테두리 토글],
  [`Pixel Format`], [#key("Ctrl+X")], [하위 메뉴 `Decimal (128)` / `Hex (0x80)` / `Hex (80h)`. 3장],
  [`Hotkey Reference`], [#key("Ctrl+Shift+H")], [전체 단축키 목록 창],
)

Diff 메뉴는 앞서 표로 정리한 10개 모드(`Off`, `Alpha Blend`, `Absolute`,
`Relative`, `Enhance`, `FalseColor`, `SSIM`, `FLIP`, `Signed`, `Highlight`)를
각자의 단축키와 함께 나열하고, 맨 아래에 `Amplify` 슬라이더(0.5\~50)를 둡니다.
이 메뉴는 소스의 `kDiffModes` 테이블 하나에서 생성되므로 메뉴·상태바·창 제목·CLI
토큰이 어긋날 일이 없습니다.

#note[`Show Histogram` / `Show H-Line Cut` / `Show V-Line Cut` /
`Show Statistics` 네 창은 서로 배타적입니다. 하나를 켜면 나머지 셋이 자동으로
닫힙니다.]

=== 이미지 패널 \u{2014} A / B / Diff

레이아웃은 로드된 영상 수와 모드에 따라 자동으로 결정됩니다.

#table(
  columns: (auto, 1fr),
  [*상황*], [*패널 배치*],
  [1장], [단일 패널 (창 전체)],
  [2장, diff 꺼짐], [A \| B (좌우 반반)],
  [2장, diff 켜짐], [A \| B \| Diff (3등분)],
  [`--comp`], [orig \| img2 \| img3 (3등분), diff 강제 해제],
  [Overlay 모드 (#key("O"))], [단일 패널에 A와 B를 겹쳐 표시],
  [Blink 모드 (#key(","))], [단일 패널에서 A와 B를 자동 교대],
)

패널 테두리 색이 곧 그 패널의 정체입니다. 기본값 기준으로 마젠타 = A(원본),
노랑 = B(비교), 시안 = Diff입니다.

=== 상태바에 표시되는 정보

상태바는 왼쪽부터 순서대로, *해당 항목이 활성일 때만* 나타납니다.

#table(
  columns: (auto, 1fr),
  [*항목*], [*내용*],
  [`Zoom: 400%`], [활성 패널의 현재 배율 (항상 표시)],
  [`A: 1920x1080`], [A 영상 해상도. 미로드 시 `A:` 뒤에 회색 대시만 표시],
  [`B: 1920x1080`], [B 영상 해상도],
  [`Diff: Abs (A-B)  x8.0`], [diff 모드 태그, 방향(스왑 시 `B-A`), 증폭 배율],
  [`SSIM: 0.9987`], [SSIM 모드일 때 점수. 계산 중이면 `computing…`. 0.99 초과 = 초록, 0.90 초과 = 노랑, 그 이하 = 빨강],
  [`FLIP: 0.0123`], [FLIP 모드일 때 평균 오차(낮을수록 좋음). 0.05 미만 = 초록, 0.15 미만 = 노랑],
  [`P2 42.1dB  P3 39.8dB  W2 120  W3 86  T 34`], [`--comp` 전용. img2/img3의 원본 대비 PSNR과 블록 승/패/무 집계 (5장)],
  [`PSNR: 41.2 dB`], [diff 모드일 때 자동 계산된 PSNR. 40dB 초과 = 초록, 30dB 초과 = 노랑, 그 이하 = 빨강. 동일 영상이면 `PSNR: inf`],
  [`[Diff: 812 / 2073600 px]`], [Highlight 모드에서 임계 초과 픽셀 수 / 전체],
  [`Thr: 4 (0.4% exceed)`], [임계값과 초과 비율],
  [`A [12/240]`], [시퀀스 위치 (현재 인덱스 / 전체 개수). A·B 각각 표시],
  [`ROI mode [x,y WxH]`], [ROI 선택 모드와 현재 영역],
  [`Overlay:Blend  50%`], [Overlay 모드와 블렌드 비율],
  [\u{25B6} 와 `1.4s / 3.0s`], [슬라이드쇼 잔여 시간 / 간격],
  [\u{25C9} 와 `BLINK A  0.50s`], [블링크 현재 위상과 간격],
  [`Crosshair`], [크로스헤어 오버레이 켜짐],
  [`Sync: on`], [줌·팬 동기화 상태 (항상 표시)],
  [`60 fps`], [오른쪽 끝에 정렬된 프레임 레이트],
)

#tip[검증 결과를 캡처할 때는 #key("U")로 상태바를 켠 채 찍으십시오.
해상도·diff 모드·PSNR·시퀀스 인덱스가 한 줄에 다 들어가므로, 캡처 이미지 자체가
"어떤 조건에서 얻은 화면인지"를 증명하는 메타데이터가 됩니다.]

=== 파일명 토스트 (1.5초)

이미지를 로드하거나 시퀀스를 넘길 때마다, 화면 *상단 중앙*에 어두운 둥근 배지가
떠서 `A:  frame_0001.png` 처럼 패널 라벨과 파일명(basename)을 1.5초간 보여 준
뒤 사라집니다. 로드에 실패하면 배경이 붉게 바뀌고 `A  (failed):  frame_0001.png`
로 표시됩니다.

이 토스트가 필요한 이유는 기본 실행이 테두리 없는 창이라 타이틀바가 없기
때문입니다. #key(";") / #key("A")로 240장짜리 시퀀스를 빠르게 넘길 때, 지금
몇 번 파일을 보고 있는지 눈으로 확인하는 유일한 수단입니다(정확한 인덱스는
상태바의 `A [12/240]`로 확인).

#note[A/B 라벨은 데이터 슬롯이 아니라 *화면상의 위치* 기준입니다.
#key("Shift+Space")로 A와 B를 스왑한 상태라면 슬롯 0의 영상이 오른쪽에
있으므로 토스트도 `B`로 표시됩니다.]

== 지원 이미지 포맷

디렉토리 시퀀스 스캔과 파일 열기가 인식하는 확장자는 다음 8종입니다.

```
.png  .jpg  .jpeg  .bmp  .tga  .pgm  .ppm  .hdr
```

- *PNG* \u{2014} 8/16비트, 알파 지원
- *JPEG* \u{2014} EXIF 방향 자동 적용
- *BMP*, *TGA* \u{2014} 기본 지원 (TGA는 알파 채널 포함)
- *HDR* \u{2014} Radiance RGBE. 내부적으로 float(RGBA32F)로 유지되어 NaN/Inf 검사(#key("/")) 대상이 됨
- *PGM/PPM* \u{2014} P2/P3(ASCII)와 P5/P6(바이너리)를 자체 파서로 읽으며, 10비트·12비트 등 `maxval`이 255가 아닌 파일도 *원본값 그대로* 보존합니다. DDI 검증에서 10비트 패널 데이터를 다룰 때 특히 중요합니다.

포맷별 비트 심도, 픽셀값 표시 우선순위(PPM 원본값 \u{2192} HDR float \u{2192} LDR
uint8), 저장 시 선택 가능한 형식은 3장과 7장에서 자세히 다룹니다.

== 종료하기

#table(
  columns: (auto, 1fr),
  [*방법*], [*동작*],
  [#key("Q")], [즉시 종료. 확인 창 없음],
  [메뉴 File \u{2192} `Quit`], [#key("Q")와 동일],
  [창 닫기 버튼 / #key("Cmd+Q") 등 OS 종료], [`SDL_EVENT_QUIT` 또는 `SDL_EVENT_WINDOW_CLOSE_REQUESTED` 수신 후 종료],
)

종료 직전에 `save_app_ini()`가 호출되어 현재 설정이 `~/.av.ini`에 기록됩니다.
저장되는 항목은 테두리 표시, 픽셀값 형식, 돋보기, 크로스헤어, 동기화, UI 표시,
각 분석 창의 열림 상태, 채널 모드, 창 모드, 오버레이, 초기 zoom 등입니다
(10장 참조).

=== Esc 키는 종료 키가 아닙니다

#key("Esc")는 *열려 있는 것을 하나씩 닫는* 키입니다. 아래 순서대로 검사해서
켜져 있는 첫 항목 하나만 끄고 멈춥니다. 따라서 창을 여러 개 띄웠다면
#key("Esc")를 여러 번 눌러야 모두 닫힙니다.

+ 블링크 모드 (끄면서 이전 sync 상태로 복원)
+ Diff 픽셀 리스트 창
+ 핫키 도움말 창
+ 히스토그램 \u{2192} H-Line Cut \u{2192} V-Line Cut \u{2192} 통계 창
+ ROI 통계 창 \u{2192} 산점도 창
+ Image Info 창 \u{2192} 픽셀값 풍선말
+ 저장 다이얼로그 \u{2192} 크로스헤어
+ ROI 선택 모드 \u{2192} Overlay 모드

아무것도 열려 있지 않으면 #key("Esc")는 아무 일도 하지 않습니다.

#warn[#key("Ctrl+C") \u{2192} #key("1")/#key("2")/#key("3") 클립보드 복사 모드가
활성일 때는 #key("Esc")가 그 모드를 취소하는 용도로 먼저 소비됩니다
(5초 후 자동 취소). 3장 참조.]

== 실무 시작 시나리오

#ex("DDI 보상 IP 회귀 검증 첫 5분")[
새 RTL 빌드가 나왔고, 원본 240프레임과 보상 결과 240프레임이 각각
`/data/ddi/orig`, `/data/ddi/hw`에 있다고 합시다.

*1단계 \u{2014} 눈으로 먼저 본다.* 8배 확대에 부호차 diff, 20배 증폭으로 시작합니다.

```
av --zoom 8 --diff-mode signed --amplify 20 -p 16 \
   /data/ddi/orig/f001.png /data/ddi/hw/f001.png
```

#key("U")로 상태바를 켜면 `PSNR: 41.2 dB`가 바로 보입니다. #key("Shift+l")을
누를 때마다 16픽셀(= 블록 하나)씩 이동하며 블록 경계를 훑고, #key(";")로 다음
프레임으로 넘기면 상단 토스트가 `A:  f002.png`를 알려 줍니다.

*2단계 \u{2014} FW 모델과 HW 결과를 동시에 본다.* 어느 쪽이 원본에 가까운지
블록 단위로 판정합니다(5장).

```
av --comp --blk 16x16 --num_blk 24 \
   /data/ddi/orig/f001.png /data/ddi/fw/f001.png /data/ddi/hw/f001.png
```

*3단계 \u{2014} 수치로 못을 박는다.* 240프레임 전체를 헤드리스로 돌려 CSV로
남기고, PSNR 게이트를 걸어 CI에서 자동 판정합니다(8장).

```
av --pair --metrics --fail-psnr 38 \
   /data/ddi/orig/f001.png /data/ddi/hw > report.csv
```
]

#tip[자주 쓰는 조합은 쉘 alias로 박아 두십시오. 예를 들어
`alias avd='av --zoom 8 --diff-mode signed --amplify 20 -p 16'` 처럼 만들어 두면
`avd orig.png comp.png` 한 줄로 검증 환경이 재현됩니다. `--zoom`은 어차피
`~/.av.ini`에 저장되지만, alias로 명시해 두면 다른 사람의 머신에서도 같은
화면이 나온다는 장점이 있습니다.]
