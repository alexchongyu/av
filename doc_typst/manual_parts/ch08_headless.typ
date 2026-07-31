#import "_common.typ": *

= 헤드리스 CLI와 CI 연동

av 는 GUI 뷰어이지만, 같은 실행 파일이 창\u{00B7}OpenGL\u{00B7}SDL 을 전혀 열지 않는
*헤드리스 모드* 로도 동작합니다. 이 장에서는 DDI 보상 IP 의 출력 프레임을 원본과
비교해 수치 리포트를 뽑고, 임계값을 넘으면 빌드를 떨어뜨리는 CI 게이트를 만드는
방법을 다룹니다. 모든 헤드리스 모드는 계산 후 즉시 종료하며, 표준 출력에는
기계가 파싱할 수 있는 순수 데이터만 흘려보냅니다.

== 헤드리스 실행 모델

=== 창을 열지 않는 모드 전수

`main()` 은 CLI 를 파싱한 직후, SDL 을 초기화하기 *전에* 아래 순서대로 헤드리스
모드를 검사합니다. 먼저 걸린 것 하나만 실행되고 프로세스는 그 반환값으로
종료합니다. 즉 여러 헤드리스 플래그를 동시에 주면 아래 표의 위쪽이 이깁니다.

#table(
  columns: (auto, auto, 1fr),
  [*순위*], [*플래그*], [*하는 일*],
  [1], [`--batch <file|->`],  [매니페스트의 임의 A,B 쌍을 지표 계산],
  [2], [`--comp-batch`],       [3개 디렉토리의 같은 이름 프레임 전체를 블록 비교],
  [3], [`--comp-out <file|->`], [3영상 블록별 CSV/JSON 테이블],
  [4], [`--metrics`],          [A 대비 B 의 PSNR/SSIM/FLIP/MSE/MAE],
  [5], [`--diff-out <png>`],   [diff 시각화를 PNG 로 저장],
  [6], [`--validate`],         [float/HDR 의 NaN/Inf/음수/\>1 스캔],
  [7], [`--probe <X,Y>`],      [픽셀 한 점의 CIE 컬러메트리],
  [8], [`--uniformity`],       [평판 균일도\u{00B7}무라 지표],
)

이 모드들은 X11/Wayland 디스플레이도, GPU 도 필요 없습니다. SSH 세션, 도커
컨테이너, GitHub Actions 러너처럼 화면이 없는 환경에서 그대로 실행됩니다.
디코딩은 전부 CPU 경로(`stb_image`)로 처리되므로 GUI 에서 보이는 것과 같은
포맷(PNG/BMP/PPM/PGM/HDR 등, 7장 참조)을 그대로 읽습니다.

=== stdout 은 데이터, stderr 는 요약

헤드리스 출력의 가장 중요한 규약은 *스트림 분리* 입니다.

- *stdout*: 헤더 한 줄 + 데이터 행만. 주석\u{00B7}진행 로그\u{00B7}합계가 섞이지 않습니다.
- *stderr*: 사람이 읽는 한 줄 요약(프레임 수, 평균/중앙값/p95, 게이트 판정,
  FAIL 프레임 이름 나열)과 오류 메시지.

따라서 `> report.csv` 로 리다이렉트하면 파일에는 순수 CSV 만 남고, 요약은
터미널\u{00B7}CI 로그에 그대로 보입니다.

```
av --metrics ref.png ip_out.png > metrics.csv     # 파일 = 순수 CSV
av --metrics ref.png ip_out.png 2> /dev/null      # 요약만 버리기
av --metrics ref.png ip_out.png > /dev/null       # 요약만 보기
```

#fig_ci_flow()

#warn[`--format json` 과 `--format junit` 에서는 stderr 요약 줄이 *출력되지
않습니다*. 집계값(mean/median/p95/min/max)과 게이트 판정은 JSON 의 `summary`
객체 안에 들어가고, JUnit 은 `tests`/`failures`/`skipped` 속성으로 대체됩니다.
CSV 모드에서만 stderr 한 줄 요약이 나옵니다.]

== 지표 추출 \u{2014} `--metrics`

=== 기본 사용법

```
av --metrics ref.png ip_out.png
```

첫 줄이 헤더, 둘째 줄부터 프레임 한 장당 한 행입니다. 실제 출력 예:

```
file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr
test_a.png,100,100,7.6564,7.6564,inf,7.6564,24.7091,0.973462,0.549758,7436.17,57.5,202,10.6353,11.8849,12.915
# stderr: [metrics] frames=1 missing=0 mean_psnr=7.6564dB median=7.6564
#         p95=7.6564 min=7.6564 mean_ssim=0.973462 mean_flip=0.549758
```

=== 16개 열의 의미

수치 필드는 모두 `%.6g` 로 찍히며, 무한대는 `inf` / `-inf` 문자열입니다.
단위 표의 "code" 는 8비트 이미지의 코드값(0\~255)을, HDR(float) 이미지는
선형광 정규화값(peak 1.0)을 뜻합니다.

#table(
  columns: (auto, auto, 1fr),
  [*열*], [*단위/범위*], [*의미*],
  [`file`],      [\u{2014}],   [A 의 파일명(basename). `--batch` 에서 label 을 주면 그 label],
  [`width`],     [px],   [A 의 가로 크기],
  [`height`],    [px],   [A 의 세로 크기],
  [`psnr_db`],   [dB],   [종합 PSNR. R/G/B 채널 PSNR 중 유한한 값만 평균 (아래 설명)],
  [`psnr_r`],    [dB],   [R 채널 PSNR (peak 255, HDR 은 1.0)],
  [`psnr_g`],    [dB],   [G 채널 PSNR],
  [`psnr_b`],    [dB],   [B 채널 PSNR],
  [`psnr_y`],    [dB],   [Rec.709 루마 Y\u{2032} PSNR. 사람 눈에 가까운 단일 수치],
  [`ssim`],      [0\~1], [구조 유사도. 1 = 완전 동일, 높을수록 좋음],
  [`flip`],      [0\~1], [FLIP 지각 오차 평균. 0 = 완전 동일, *낮을수록* 좋음],
  [`mse`],       [code²],[R/G/B MSE 의 산술평균],
  [`mae`],       [code], [R/G/B MAE 의 산술평균],
  [`max_error`], [code], [세 채널을 통틀어 가장 큰 절대오차 한 픽셀],
  [`msigned`],   [code], [루마 가중 평균 *부호* 오차 (밝기 편향). 아래 설명],
  [`psnr_cb`],   [dB],   [Rec.709 크로마 Cb PSNR (Cb = (B\u{2212}Y)/1.8556)],
  [`psnr_cr`],   [dB],   [Rec.709 크로마 Cr PSNR (Cr = (R\u{2212}Y)/1.5748)],
)

SSIM\u{00B7}FLIP 의 알고리즘적 의미와 GUI 상의 히트맵 표현은 4장을 참조하세요.
여기서는 CSV 로 떨어지는 스칼라 점수만 다룹니다.

=== `psnr_db` \u{2014} "채널별 PSNR 평균" 관례

av 의 종합 PSNR 은 전체 MSE 하나로 계산하지 않고, *R/G/B 각각의 PSNR 을 구한 뒤
유효한 것만 평균* 냅니다. 유효 조건은 `0 < psnr < 999` 이므로, 어떤 채널이 A 와
B 에서 완전히 동일해 PSNR 이 무한대가 되면 그 채널은 평균에서 *빠집니다*.
세 채널이 모두 동일하면 남는 값이 없으므로 `psnr_db` 자체가 `inf` 가 됩니다.

이 관례는 GUI 의 Image Info 창(`i`)에 표시되는 PSNR, `--comp` 의 전역 PSNR 과
완전히 같습니다(3장\u{00B7}5장 참조). 따라서 화면에서 본 값과 CI 리포트의 값이
어긋나지 않습니다.

#warn[위 예시 행에서 `psnr_g` 가 `inf` 인데 `psnr_db` 는 7.6564 입니다. G 채널이
평균에서 빠져 R/B 두 채널만 평균된 결과입니다. 보상 IP 가 특정 채널만 손대는
경우(예: R 게인만 보정) 종합 PSNR 이 실제보다 *나쁘게* 보일 수 있으므로,
채널별 `psnr_r`/`psnr_g`/`psnr_b` 열을 함께 확인하세요.]

=== `msigned` \u{2014} 밝기 편향 읽는 법

`msigned` 는 채널별 평균 부호 오차 `mean(A[c] - B[c])` 를 Rec.709 가중치로
합친 값입니다.

```
msigned = 0.2126*mean(Ar-Br) + 0.7152*mean(Ag-Bg) + 0.0722*mean(Ab-Bb)
```

절대값 지표(`mse`, `mae`)와 달리 부호가 살아 있으므로 *어느 쪽으로 치우쳤는지*
를 알려줍니다.

- `msigned` \> 0 \u{2192} A(원본)가 평균적으로 더 밝음 = B(보상 출력)가 어두워짐
- `msigned` \< 0 \u{2192} 보상 출력이 원본보다 밝아짐
- `msigned` \u{2248} 0 인데 `mae` 가 크면 \u{2192} 편향 없는 노이즈성 오차

#tip[DDI 보상 IP 검증에서 `msigned` 는 감마\u{00B7}오프셋 계수 부호 실수를 잡는
가장 빠른 신호입니다. `mae` 는 그대로인데 `msigned` 만 프레임 전체에서 한쪽
부호로 몰려 있으면, 알고리즘 오차가 아니라 DC 오프셋/게인 세팅 오류일
가능성이 높습니다.]

=== 시퀀스 전체 실행 \u{2014} `--pair` 조합

`--pair` 를 함께 주면 A 의 디렉토리를 전부 훑어 같은 파일명을 B 디렉토리에서
찾아 짝짓고, *프레임당 한 행* 을 출력합니다. 파일 순서는 GUI 의 next/prev 와
동일한 파일명 코드포인트 정렬입니다(7장 참조).

```
av --pair --metrics golden/f0001.png build/ip_out
```

- 첫 번째 인자는 A 디렉토리 안의 *파일* 이어야 합니다(디렉토리만 주면 안 됨).
- 두 번째 인자는 B 디렉토리 자체 또는 그 안의 아무 파일이나 줄 수 있습니다.
- 두 디렉토리가 같으면 비교 의미가 없으므로 stderr 경고 후 exit 1 입니다.
- B 에 같은 이름이 없으면 그 행은 `file,,,missing,,...` 플레이스홀더가 되고
  stderr 요약의 `missing=` 카운트에 잡힙니다.
- 디코드 실패는 `decode_error`, 크기/포맷 불일치는 `mismatch` 로 표시됩니다.

#ex("시퀀스 회귀 리포트")[
```
av --pair --metrics golden/f0001.png build/ip_out > run.csv
# stdout(run.csv):
#   file,width,height,psnr_db,...
#   f0001.png,1080,2392,41.2831,...
#   f0002.png,1080,2392,40.9773,...
# stderr:
#   [metrics] frames=2 missing=0 mean_psnr=41.1302dB median=41.1302
#             p95=41.2529 min=40.9773 mean_ssim=0.9971 mean_flip=0.0184
```
]

=== 출력 포맷 \u{2014} `--format csv|json|junit`

기본값은 `csv` 입니다. 값은 `csv`, `json`, `junit` 셋 중 하나여야 하며
다른 값을 주면 즉시 오류 종료합니다.

*csv (기본)* \u{2014} 위에서 본 16열 헤더 + 행. stderr 에 한 줄 요약.

*json* \u{2014} 프레임 배열 + 집계 요약. 무한대는 JSON 숫자로 표현할 수 없으므로
`null` 이 됩니다. 게이트를 켰다면 프레임마다 `verdict`, 요약에 `gate` 가 붙습니다.

```
av --metrics ref.png ip_out.png --format json --fail-psnr 30
```
```
{
  "frames": [
    {"file":"test_a.png","width":100,"height":100,"psnr_db":7.6564,
     "psnr_r":7.6564,"psnr_g":null,"psnr_b":7.6564,"psnr_y":24.7091,
     "ssim":0.973462,"flip":0.549758,"mse":7436.17,"mae":57.5,
     "max_error":202,"msigned":10.6353,"psnr_cb":11.8849,"psnr_cr":12.915,
     "verdict":"FAIL"}
  ],
  "summary": {"frames":1,"missing":0,
    "psnr":{"mean":7.6564,"median":7.6564,"p95":7.6564,"min":7.6564,"max":7.6564},
    "ssim":{"mean":0.973462,...},"flip":{"mean":0.549758,...},
    "gate":{"verdict":"FAIL","fail":1,"warn":0}}
}
```

행이 `missing`/`decode_error`/`mismatch` 이면 수치 필드 대신
`{"file":"...","status":"missing"}` 형태로만 나옵니다.

*junit* \u{2014} CI 서버가 바로 읽는 테스트 리포트 XML. 프레임 하나가 testcase
하나이고, 게이트 FAIL 이 `<failure>`, 플레이스홀더 행이 `<skipped>` 입니다.

```
av --metrics ref.png ip_out.png --format junit --fail-psnr 30 > junit.xml
```
```
<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="av-metrics" tests="1" failures="1" skipped="0">
  <testcase name="test_a.png"><failure message="metric gate">psnr=7.6564 ssim=0.973462 flip=0.549758 maxerr=202</failure></testcase>
</testsuite>
```

#note[`--format` 은 `--metrics` 와 `--batch` 에 적용됩니다. `--comp-out` 은
`csv` 와 `json` 만 지원하고 `junit` 을 주면 stderr 경고 후 CSV 로 떨어집니다.
`--probe`/`--uniformity`/`--validate`/`--comp-batch` 는 항상 CSV 입니다.]

== CI 게이트 \u{2014} `--fail-*` / `--warn-psnr`

=== 다섯 개의 임계값

게이트 옵션 중 하나라도 주면 게이트가 활성화되고, 프레임마다 PASS/WARN/FAIL
판정이 붙습니다. 지정하지 않은 임계값은 내부적으로 `-1` 이라 검사되지 않습니다.
*방향에 주의하세요* \u{2014} PSNR/SSIM 은 "낮으면 실패", FLIP/MaxErr 는 "높으면
실패" 입니다.

#table(
  columns: (auto, auto, 1fr),
  [*옵션*], [*방향*], [*판정*],
  [`--fail-psnr <dB>`],   [`psnr_db < dB`],     [FAIL. 단 `psnr_db` 가 `inf`(완전 동일)면 검사 자체를 건너뜀],
  [`--warn-psnr <dB>`],   [`psnr_db < dB`],     [WARN. 종료 코드에는 영향 없음(0 유지)],
  [`--fail-ssim <v>`],    [`ssim < v`],         [FAIL. 동일 영상은 `ssim`=1 이라 통과],
  [`--fail-flip <v>`],    [`flip > v`],         [FAIL. FLIP 은 낮을수록 좋으므로 부등호 방향이 반대],
  [`--fail-maxerr <v>`],  [`max_error > v`],    [FAIL. 단일 픽셀 스파이크를 잡는 용도],
)

한 프레임이라도 FAIL 이면 프로세스는 *exit 10* 으로 끝납니다. WARN 만 있으면
exit 0 입니다(로그에만 표시). 게이트는 `--metrics` 와 `--batch` 에서 동작하고,
`--uniformity` 는 별도의 `--fail-uniformity`/`--fail-semu` 를 씁니다.

=== 게이트 출력

CSV 모드에서는 stderr 요약 줄 끝에 `| GATE ...` 가 붙고, 그다음 줄에 문제
프레임 목록이 나열됩니다.

#ex("게이트 FAIL 시 stderr")[
```
av --pair --metrics golden/f001.png out/ --fail-psnr 30 --warn-psnr 40
# stderr:
# [metrics] frames=2 missing=0 mean_psnr=7.6564dB median=7.6564 p95=7.6564
#           min=7.6564 mean_ssim=0.973462 mean_flip=0.549758
#           | GATE FAIL fail=2 warn=0
# [metrics] FAIL:f001.png FAIL:f002.png
# exit code: 10
```
]

#tip[DDI 양산 회귀에서는 `--fail-psnr` 을 "절대 넘으면 안 되는 하한"으로,
`--warn-psnr` 을 "품질이 서서히 떨어지는 신호"로 두 단계로 거는 것이 안전합니다.
예: `--fail-psnr 38 --warn-psnr 42`. 여기에 `--fail-maxerr 8` 을 더하면 평균은
멀쩡한데 한두 픽셀만 크게 튀는 보상 LUT 경계 아티팩트를 잡을 수 있습니다.]

== 픽셀 컬러메트리 \u{2014} `--probe <X,Y>`

=== 무엇을 출력하나

이미지 A(그리고 선택적으로 B)의 픽셀 좌표 (X,Y) 한 점을 CIE 값으로 변환해
11열 CSV 로 출력합니다. GUI 의 #key("Shift+V") 컬러메트리 벌룬(3장 참조)과 같은
색 변환 코어를 씁니다.

```
av --probe 540,1196 panel_white.png
av --probe 540,1196 ref.png ip_out.png     # A / B / delta 세 행
```

#table(
  columns: (auto, auto, 1fr),
  [*열*], [*단위*], [*의미*],
  [`which`],  [\u{2014}], [행 종류: `A` / `B` / `delta`],
  [`X`],      [\u{2014}], [CIE 1931 삼자극치 X (D65)],
  [`Y`],      [\u{2014}], [CIE 1931 Y \u{2014} 상대 휘도],
  [`Z`],      [\u{2014}], [CIE 1931 Z],
  [`x`],      [\u{2014}], [CIE 1931 색도 x],
  [`y`],      [\u{2014}], [CIE 1931 색도 y],
  [`uprime`], [\u{2014}], [CIE 1976 u\u{2032}],
  [`vprime`], [\u{2014}], [CIE 1976 v\u{2032}],
  [`CCT`],    [K],  [McCamy 근사 상관색온도],
  [`Duv`],    [\u{2014}], [흑체 궤적으로부터의 편차],
  [`Lstar`],  [0\~100], [CIE L\* (명도)],
)

8비트 이미지는 화소값을 sRGB 감마 인코딩된 표시값으로 보고 디코드하며,
HDR(float) 이미지는 선형광 그대로 취급합니다.

=== 검증된 기준값

순백색(255,255,255) 픽셀을 찍으면 sRGB 의 정의상 D65 백색점이 정확히 나와야
합니다. 실제 실행 결과입니다.

#ex("D65 백색점 확인")[
```
av --probe 32,32 white.png
```
```
which,X,Y,Z,x,y,uprime,vprime,CCT,Duv,Lstar
A,0.950456,1,1.08906,0.3127,0.329,0.19783,0.46832,6505.08,0.00320742,100
```
`x`=0.3127, `y`=0.3290, `CCT`\u{2248}6504K, `Lstar`=100 \u{2014} 교과서상의 D65 값과
일치합니다. 색 파이프라인이 정상인지 CI 에서 확인하는 스모크 테스트로 그대로
쓸 수 있습니다.
]

=== B 를 함께 준 경우의 `delta` 행

두 번째 이미지를 주면 `A`, `B` 행에 이어 `delta` 행이 나옵니다. 이 행은 앞의
XYZ/xy/u\u{2032}v\u{2032} 칸을 *비워 두고* 뒤의 세 칸만 채웁니다.

- `CCT` 칸 \u{2192} \u{0394}CCT (B \u{2212} A, 단위 K)
- `Duv` 칸 \u{2192} \u{0394}u\u{2032}v\u{2032} (두 점 사이의 색도 거리)
- `Lstar` 칸 \u{2192} \u{0394}E76 (CIE 1976 색차)

```
which,X,Y,Z,x,y,uprime,vprime,CCT,Duv,Lstar
A,...,...
B,...,...
delta,,,,,,,,-118.4,0.00214,1.83
```

#tip[보상 전/후 패널의 같은 좌표를 찍어 \u{0394}u\u{2032}v\u{2032} 를 보면 보상 IP 가
색을 얼마나 틀어놓았는지 한 줄로 확인됩니다. 일반적으로 \u{0394}u\u{2032}v\u{2032}
0.004 정도가 육안 식별 한계 근처이므로, 이를 CI 임계로 쓰기 좋습니다(수치는
스크립트에서 직접 비교 \u{2014} `--probe` 에는 게이트 옵션이 없습니다).]

좌표가 이미지 범위를 벗어나면 stderr 에 범위 안내를 출력하고 exit 3 입니다.

== 평판 균일도와 무라 \u{2014} `--uniformity`

=== 무엇을 재나

풀 화이트/그레이 같은 *플랫 필드* 영상 한 장을 받아 휘도 균일도, 비균일도,
색 편차, 무라 지수를 한 번에 계산합니다. 패널 점등 검사 이미지나 보상 전후
플랫 필드를 CI 로 추적할 때 씁니다.

```
av --uniformity flat_white.png
av --uniformity flat_before.png flat_after.png
av --pair --uniformity flat/f001.png flat_after/
```

=== 11개 열

```
file,role,width,height,Lmean,uni9_pct,uni13_pct,uni25_pct,nonuni_cv_pct,duv_max,semu
```

#table(
  columns: (auto, 1fr),
  [*열*], [*의미*],
  [`file`],          [이미지 파일명. \u{0394} 행은 `delta`],
  [`role`],          [`A` / `B` / `B-A`],
  [`width`,`height`],[픽셀 크기],
  [`Lmean`],         [화면 전체 평균 CIE Y (상대 휘도). 8비트는 sRGB 디코드, HDR 은 선형광],
  [`uni9_pct`],      [ICDM 9점 휘도 균일도 % = 100\u{00B7}Lmin/Lmax],
  [`uni13_pct`],     [ICDM 13점 균일도 %],
  [`uni25_pct`],     [ICDM 25점 균일도 %],
  [`nonuni_cv_pct`], [전 픽셀 변동계수 % = 100\u{00B7}\u{03C3}/\u{03BC}. *낮을수록* 좋음],
  [`duv_max`],       [25개 측정점 u\u{2032}v\u{2032} 중 화면 평균 u\u{2032}v\u{2032} 로부터의 최대 거리],
  [`semu`],          [SEMU 무라 지수 프록시. 평탄하면 0, 클수록 무라가 눈에 띔],
)

=== 균일도 % 의 정의와 측정점 배치

`uni9`/`uni13`/`uni25` 는 모두 `100 * Lmin / Lmax` 입니다. *100% 가 완벽* 이고
값이 클수록 좋습니다(CV 와 방향이 반대이니 혼동에 주의하세요). 각 측정점은
한 픽셀이 아니라 반지름 `max(1, min(W,H)/40)` 인 정사각 박스 평균을 씁니다 \u{2014}
센서 노이즈나 디더 패턴에 흔들리지 않게 하기 위한 처리입니다.

측정점 격자는 ICDM 관례를 따릅니다.

- *9점*: 가로/세로 각각 1/6, 3/6, 5/6 위치의 3\u{00D7}3
- *13점*: 위 9점 + 1/3, 2/3 위치의 안쪽 2\u{00D7}2 네 점
- *25점*: 가로/세로 각각 1/10, 3/10, 5/10, 7/10, 9/10 의 5\u{00D7}5

`nonuni_cv_pct` 는 격자 점이 아니라 *전 픽셀* 의 표준편차/평균이므로, 격자
사이에 숨은 얼룩까지 반영합니다. 균일도 %는 좋은데 CV 가 크다면 측정점을
비껴간 국부 결함이 있다는 뜻입니다.

=== SEMU 무라 지수 \u{2014} 계산 방식과 한계

`semu` 는 다음 순서로 계산됩니다.

+ 휘도 필드에 2차 다항식 배경면 `{1, x, y, x*x, x*y, y*y}` 을 최소제곱으로
  피팅해 뺍니다. 부드러운 셰이딩\u{00B7}기울기\u{00B7}비네팅은 이 단계에서
  깨끗이 제거되므로, 순수 그라디언트 영상은 잔차가 거의 0 이 됩니다
  (그건 무라가 아니라 셰이딩이니까요).
+ 잔차의 Weber 대비 맵에서 최대값 Cmax 를 구합니다.
+ `|c| >= 0.5 * Cmax` 인 픽셀 면적 S 를 프레임 대비 % 로 구합니다.
+ `semu = 100 * Cmax / (1.97 / S^0.33 + 0.72)` 로 가시성 정규화를 적용합니다.

#warn[`semu` 는 *프록시* 입니다. 표준 SEMU 정의는 결함의 물리적 크기와 시청
거리를 요구하지만, av 는 이미지 한 장만 보므로 면적을 "프레임 대비 %" 로
가정합니다. 실제 픽셀 피치\u{00B7}시청 거리는 모델링되지 않으므로 *절대
합격/불합격 판정에 쓰지 말고*, 같은 해상도\u{00B7}같은 촬영 조건에서의 상대
추세(보상 전 대비 후, 리비전 간 비교)로만 사용하세요. 실행할 때마다 stderr 에도
같은 경고가 찍힙니다.]

=== A/B/\u{0394} 세 행

이미지를 두 장 주거나 `--pair` 를 쓰면 A 행, B 행에 이어 `delta,B-A` 행이
따라옵니다. \u{0394} 행은 단순 차(B \u{2212} A)이므로 부호 해석이 중요합니다.

- `uni*_pct` 가 *양수* \u{2192} 보상 후 균일도가 좋아짐
- `nonuni_cv_pct` 가 *음수* \u{2192} 비균일도가 줄어듦 (개선)
- `semu` 가 *음수* \u{2192} 무라가 줄어듦 (개선)

#ex("보상 전후 균일도 비교")[
```
av --uniformity flat.png flat_mura.png
```
```
file,role,width,height,Lmean,uni9_pct,uni13_pct,uni25_pct,nonuni_cv_pct,duv_max,semu
flat.png,A,256,256,0.57758,100,100,100,0.000123628,1.73743e-14,0
flat_mura.png,B,256,256,0.571199,74.2218,74.2218,74.2218,5.2796,1.38294e-14,11.8958
delta,B-A,256,256,-0.00638171,-25.7782,-25.7782,-25.7782,5.27948,-3.54484e-15,11.8958
```
완전 평탄한 A 는 균일도 100%, CV \u{2248} 0, SEMU 0 입니다. 중앙에 원형 얼룩을
넣은 B 는 균일도 74.2%, CV 5.28%, SEMU 11.9 로 떨어지고 \u{0394} 행이 정확히
그 악화폭을 보여줍니다.
]

=== 균일도 게이트

#table(
  columns: (auto, 1fr),
  [*옵션*], [*판정*],
  [`--fail-uniformity <pct>`], [`uni25_pct < pct` 이면 FAIL. 25점 균일도 기준],
  [`--fail-semu <v>`],         [`semu > v` 이면 FAIL],
)

```
av --uniformity flat_after.png --fail-uniformity 88 --fail-semu 1.5
```

하나라도 FAIL 이면 exit 10, 아니면 0 입니다. A 행과 B 행 모두 각각 판정되며
(\u{0394} 행은 판정 대상이 아님), stderr 에 `| GATE FAIL fail=N` 이 붙습니다.
디코드에 실패하면 exit 4 입니다.

== 매니페스트 배치 \u{2014} `--batch <file|->`

=== 왜 필요한가

`--pair` 는 "두 디렉토리에 같은 파일명" 이라는 제약이 있습니다. `--batch` 는
그 제약을 없애고, *임의의 A,B 쌍 목록* 을 파일 하나로 넘겨 한 번에 처리합니다.
파일명이 서로 다른 골든 이미지와 IP 출력, 여러 테스트 케이스를 섞은 회귀
스위트에 적합합니다.

=== 매니페스트 형식

- 한 줄에 한 쌍: `A<TAB>B` 또는 `A<TAB>B<TAB>label`
- 구분자는 반드시 *TAB* (공백 아님). 각 필드의 앞뒤 공백/탭은 제거됩니다.
- `#` 로 시작하는 줄은 주석, 빈 줄(공백/탭만 있는 줄 포함)은 무시
- CRLF 줄바꿈도 그대로 처리됩니다
- TAB 이 없는 줄은 stderr 경고 후 건너뜁니다
- `label` 을 주면 CSV `file` 열에 그 label 이 들어갑니다(없으면 A 의 basename)

#ex("매니페스트 파일 예 (pairs.tsv)")[
```
# DDI 보상 IP 회귀 스위트
# A(원본)<TAB>B(보상 출력)<TAB>label
golden/gradient.png	out/gradient_fw.png	gradient-fw
golden/gradient.png	out/gradient_hw.png	gradient-hw
golden/skin_tone.png	out/skin_tone_hw.png	skin

# 코너 케이스 (파일명이 서로 달라도 됨)
ref/checker_1080.png	out/checker_result.png	checker
ref/checker_1080.png	ref/checker_1080.png	self-identity
```
]

=== 실행

```
av --batch pairs.tsv                          # 파일에서 읽기
cat pairs.tsv | av --batch -                  # stdin (파이프)
av --batch pairs.tsv --format junit > junit.xml
av --batch pairs.tsv --fail-psnr 38 --fail-maxerr 8
```

출력은 `--metrics` 와 완전히 동일한 16열이고, `--format` 과 모든 `--fail-*` /
`--warn-psnr` 게이트가 그대로 적용됩니다.

#ex("배치 실행 결과")[
```
av --batch pairs.tsv
```
```
file,width,height,psnr_db,psnr_r,psnr_g,psnr_b,psnr_y,ssim,flip,mse,mae,max_error,msigned,psnr_cb,psnr_cr
frame01,100,100,7.6564,7.6564,inf,7.6564,24.7091,0.973462,0.549758,7436.17,57.5,202,10.6353,11.8849,12.915
identical,100,100,inf,inf,inf,inf,inf,1,0,0,0,0,0,inf,inf
# stderr: [batch] frames=2 missing=0 mean_psnr=7.6564dB median=7.6564
#         p95=7.6564 min=7.6564 mean_ssim=0.986731 mean_flip=0.274879
```
`identical` 행처럼 완전히 같은 두 파일은 모든 PSNR 이 `inf`, `ssim`=1,
`flip`=0 이 됩니다. `inf` 행은 평균 PSNR 집계에서 제외되므로 `mean_psnr` 이
7.6564 로 남아 있는 점에 주의하세요(SSIM/FLIP 평균에는 포함됩니다).
]

파일을 못 열면 exit 3, 유효한 쌍이 하나도 없어도 exit 3 입니다.

== diff 이미지 내보내기 \u{2014} `--diff-out <png>`

=== 기본 사용법

GUI 를 띄우지 않고 4장의 diff 시각화를 PNG 파일로 바로 저장합니다. CI 가
FAIL 한 프레임의 "증거 이미지"를 아티팩트로 남길 때 씁니다.

```
av --diff-out diff.png ref.png ip_out.png
av --diff-out diff.png --diff-mode signed --amplify 8 ref.png ip_out.png
av --diff-out diff.png --diff-mode flip --sbs ref.png ip_out.png
```

- `--diff-mode` 를 생략하면(기본 `none`) 절대차(abs)로 저장합니다.
- `--amplify <val>` 은 차이 증폭 배율입니다. 기본 `1.0`, 유효 범위 0.1\~100.
  미세한 보상 오차를 눈으로 보이게 하려면 8\~30 정도를 씁니다.
- 저장 후 stderr 에 `[diff-out] wrote <경로> (WxH)` 를 찍고 exit 0 입니다.

=== `--sbs` 합성

`--sbs` 를 붙이면 A \| \u{0394} \| B 를 가로로 이어 붙인 폭 3배 이미지를
저장합니다. 리뷰어가 원본과 결과를 나란히 놓고 볼 수 있어 PR 코멘트에
첨부하기 좋습니다.

#ex("증거 이미지 만들기")[
```
av --diff-out build/diff_f0007.png --diff-mode signed --amplify 8 --sbs \
   golden/f0007.png build/ip_out/f0007.png
# stderr: [diff-out] wrote build/diff_f0007.png (3240x2392)
```
입력이 1080\u{00D7}2392 였다면 `--sbs` 로 3240\u{00D7}2392 가 됩니다.
`signed` 모드라 보상 출력이 원본보다 어두운 곳은 붉게, 밝은 곳은 푸르게
나타나 편향 방향이 한눈에 보입니다.
]

=== 주의점

#warn[헤드리스 diff 는 CPU 경로로 재현되므로 GUI 셰이더와 완전히 같은 모드
집합을 갖지 않습니다.
`abs`\u{00B7}`rel`\u{00B7}`falsecolor`\u{00B7}`signed` 는 그대로 재현되고,
`flip` 은 FLIP 엔진의 magma 히트맵으로 저장됩니다. 그러나 `ssim` 은 절대차로
대체되고, `alphablend`\u{00B7}`enhance`\u{00B7}`highlight` 도 절대차로 떨어집니다.
정확한 SSIM 맵이 필요하면 GUI 에서 저장하세요(4장 참조).]

#note[`--diff-out` 은 *단일 쌍 전용* 입니다. `--pair` 를 같이 줘도 시퀀스를
순회하지 않고 A/B 한 쌍만 처리합니다. 시퀀스 전체의 diff 가 필요하면
아래 CI 스크립트 예처럼 셸 루프로 돌리세요. 두 이미지의 크기가 다르면
exit 5, FLIP 계산 실패는 exit 6, PNG 쓰기 실패는 exit 7 입니다.]

== 결함 스캔 \u{2014} `--validate`

=== 무엇을 잡나

float/HDR 이미지 한 장을 훑어 표현 불가능하거나 범위를 벗어난 픽셀을 셉니다.
R/G/B 세 채널만 검사하며 알파는 보지 않습니다.

```
av --validate ip_out.hdr
```

```
class,count
nan,0
inf,0
negative,0
superwhite,1440
# first offenders: class,x,y,channel
# superwhite,17,0,R
# superwhite,17,0,G
# superwhite,17,0,B
```

#table(
  columns: (auto, auto, 1fr),
  [*class*], [*조건*], [*의미*],
  [`nan`],        [`isnan(v)`], [NaN. 0 나눗셈\u{00B7}미초기화 버퍼의 전형적 증상],
  [`inf`],        [`isinf(v)`], [무한대. 오버플로/발산],
  [`negative`],   [`v < 0`],    [음수 광량. 보상 계수가 과보정한 결과],
  [`superwhite`], [`v > 1`],    [1.0 초과. HDR 이면 정상일 수 있음],
)

처음 발견된 최대 20개는 `#` 주석 줄로 `class,x,y,channel` 위치까지 알려줍니다.
CSV 파서는 이 줄을 주석으로 건너뛰면 됩니다.

=== 종료 코드와 8비트 입력

- `nan` 또는 `inf` 가 하나라도 있으면 *exit 8*. `negative`/`superwhite` 만
  있으면 exit 0 입니다(HDR 에서는 정상 범주일 수 있으므로).
- 8비트 이미지를 주면 구조상 이런 값이 불가능하므로 카운트를 전부 0 으로 찍고
  stderr 에 `8-bit image (no float data) — nothing to validate` 를 남긴 뒤
  exit 0 으로 끝납니다.

#tip[보상 IP 의 float 중간 출력(.hdr)을 CI 초입에서 `--validate` 로 한 번
걸러두면, NaN 이 섞인 프레임으로 PSNR 을 계산하다가 원인 불명의 이상값을
쫓는 시간을 아낄 수 있습니다. GUI 에서 같은 검사를 오버레이로 보려면
#key("/") 키를 쓰세요(3장 참조).]

== 블록 비교 헤드리스 \u{2014} `--comp-out` / `--comp-batch`

5장의 3영상 블록 비교(`--comp`)를 CI 에서 수치로 뽑는 두 가지 모드입니다.
둘 다 `--comp` 모드를 요구하며, 위치 인자 세 개(원본, img2, img3)가 모두
있어야 합니다(세 번째 인자를 주면 `--comp` 는 자동 활성화됩니다).

=== 블록별 테이블 \u{2014} `--comp-out`

```
av --comp orig.png fw.png hw.png --comp-out blocks.csv --blk 16x16
av --comp orig.png fw.png hw.png --comp-out -            # stdout 으로
av --comp orig.png fw.png hw.png --comp-out - --format json
```

인자가 `-` 이면 stdout, 그 외에는 그 경로의 파일에 씁니다(열 수 없으면 exit 3).
행은 *전체 블록* 이며 row-major(`by` 바깥 루프, `bx` 안쪽 루프) 순서입니다.

#table(
  columns: (auto, 1fr),
  [*열*], [*의미*],
  [`bx`,`by`],       [블록 그리드 인덱스 (0-based)],
  [`x`,`y`],         [블록 좌상단 픽셀 좌표],
  [`w`,`h`],         [실제 블록 크기. 오른쪽/아래 가장자리는 잘려서 작아짐],
  [`mse_img2`],      [img2 블록 MSE (R/G/B 평균)],
  [`psnr_img2`],     [img2 블록 PSNR dB. MSE 가 0 이면 `inf`],
  [`mse_img3`],      [img3 블록 MSE],
  [`psnr_img3`],     [img3 블록 PSNR dB],
  [`dpsnr`],         [`psnr_img2 - psnr_img3`. 양수 = img2 우세],
)

`dpsnr` 의 특수값: img2 만 완전 일치면 `inf`, img3 만 완전 일치면 `-inf`,
둘 다 완전 일치면 `0` 입니다.

stderr 에는 승패 요약 한 줄이 나옵니다. 승패 기준은 블록 MSE 비의
`d = 10*log10(mse_img3 / mse_img2)` 로, `d > 1` 이면 img2 승, `d < -1` 이면
img3 승, 그 사이는 무승부입니다(즉 1 dB 이내는 동률로 봅니다).

#ex("블록 테이블 뽑기")[
```
av --comp orig.png fw.png hw.png --comp-out - --blk 32x32
```
```
bx,by,x,y,w,h,mse_img2,psnr_img2,mse_img3,psnr_img3,dpsnr
0,0,0,0,32,32,448.583,21.6124,0,inf,-inf
1,0,32,0,32,32,3698.15,12.4510,0,inf,-inf
2,0,64,0,32,32,15823.6,6.1378,0,inf,-inf
3,0,96,0,4,32,26141.7,3.9575,0,inf,-inf
...
# stderr:
# [comp] blocks=16 nonzero=16 img2_psnr=7.6564 img3_psnr=999
#        | wins img2=0 img3=16 tie=0 (|d|>1dB)
```
마지막 열의 `w=4` 는 폭 100px 이미지를 32px 블록으로 나눌 때 남는 자투리입니다.
stderr 의 `img3_psnr=999` 는 전역 PSNR 이 무한대(완전 일치)라는 내부 표현입니다.
]

#warn[헬프 문구와 달리, `--comp-out` 이 실제로 반영하는 것은 `--blk` 뿐입니다.
CSV 는 *모든* 블록을 출력하므로 `--num_blk` 는 행 수를 바꾸지 않고(그 옵션은
GUI 의 worst 박스 개수 전용), `--blk-metric` 도 열 값에 영향을 주지 않습니다
(`mse_*`/`psnr_*` 열은 항상 RGB 기준). 랭킹 기준을 바꿔 보고 싶다면
`--comp-batch` 의 `worst2`/`worst3` 열이나 GUI 를 쓰세요.]

`--format json` 을 주면 같은 필드를 객체 배열로 냅니다. `inf` 표현을 위해
`psnr_img2`/`psnr_img3`/`dpsnr` 은 *문자열* 로 인코딩됩니다.

```
[
  {"bx":0,"by":0,"x":0,"y":0,"w":32,"h":32,
   "mse_img2":448.583,"psnr_img2":"21.6124",
   "mse_img3":0,"psnr_img3":"inf","dpsnr":"-inf"},
  ...
]
```

=== 프레임 요약 \u{2014} `--comp-batch`

세 이미지 각각의 *디렉토리* 에서 같은 이름을 가진 프레임을 전부 훑어,
프레임당 한 행으로 요약합니다. 알고리즘 리비전 두 개(예: FW 레퍼런스 vs HW RTL)
의 추세를 CI 에서 추적하는 데 씁니다.

```
av --comp orig/f001.png fw/f001.png hw/f001.png --comp-batch --blk 16x16
```

#table(
  columns: (auto, 1fr),
  [*열*], [*의미*],
  [`file`],       [프레임 파일명],
  [`psnr_img2`],  [img2 의 원본 대비 전역 PSNR dB (완전 일치는 `inf`)],
  [`psnr_img3`],  [img3 의 전역 PSNR dB],
  [`dpsnr`],      [`psnr_img2 - psnr_img3`. 어느 한쪽이 `inf` 면 `0`],
  [`win2`],       [img2 가 1 dB 이상 이긴 블록 수],
  [`win3`],       [img3 가 1 dB 이상 이긴 블록 수],
  [`tie`],        [1 dB 이내 무승부 블록 수],
  [`worst2`],     [img2 의 \#1 worst 블록. `bx:by@psnr` 형식, 없으면 `-`],
  [`worst3`],     [img3 의 \#1 worst 블록],
  [`status`],     [`ok` / `missing` (짝 없음\u{00B7}디코드 실패) / `mismatch` (크기 불일치)],
)

`--blk-metric rgb|y|chroma` 는 `worst2`/`worst3` 의 랭킹 기준에 실제로
반영됩니다(기본 `rgb`). `--blk` 는 블록 크기를, 따라서 승패 카운트를 바꿉니다.

#ex("리비전 추세 요약")[
```
av --comp orig/f001.png fw/f001.png hw/f001.png --comp-batch --blk 16x16
```
```
file,psnr_img2,psnr_img3,dpsnr,win2,win3,tie,worst2,worst3,status
f001.png,7.6564,inf,0,0,49,0,6:6@3.96,-,ok
f002.png,7.6564,7.6564,0.0000,0,0,49,6:6@3.96,6:6@3.96,ok
# stderr:
# [comp-batch] frames=2 missing=0 mean_psnr img2=7.6564 img3=7.6564
#              | total wins img2=0 img3=49 tie=49 (|d|>1dB)
```
`f001.png` 는 img3 가 원본과 완전히 같아 `psnr_img3=inf`, `worst3=-` 이고
49개 블록 전부 img3 승입니다. `worst2=6:6@3.96` 은 그리드 (6,6) 블록의 PSNR 이
3.96 dB 로 가장 나쁘다는 뜻입니다 \u{2014} 이 좌표를 GUI 에 그대로 입력해
현장 확인할 수 있습니다(5장 참조).
]

유효 프레임이 하나도 없으면 exit 3 입니다. 두 comp 모드 모두 `--fail-*`
게이트를 지원하지 않으므로, 임계 판정은 CSV 를 받아 스크립트에서 하세요.

== 종료 코드

CI 스크립트는 av 의 종료 코드만 보고도 무슨 일이 있었는지 구분할 수 있습니다.

#table(
  columns: (auto, 1fr),
  [*코드*], [*의미*],
  [`0`],  [성공. WARN 만 발생한 경우도 0 입니다],
  [`1`],  [CLI 파싱\u{00B7}검증 실패. 알 수 없는 옵션, 잘못된 `--blk`/`--format`/hex, 값 누락, `--comp` 에 이미지 3장 미달, `--comp-out`/`--comp-batch` 를 `--comp` 없이 사용, `--pair` 의 두 디렉토리가 동일],
  [`2`],  [SDL/윈도우/ImGui 초기화 실패. GUI 경로에서만 발생],
  [`3`],  [헤드리스 인자 오류. 이미지 인자 부족, `--probe` 좌표 형식/범위 오류, 매니페스트 열기 실패, 유효 쌍 0개, `--comp-out` 파일 열기 실패, `--comp-batch` 유효 프레임 0개],
  [`4`],  [이미지 디코드 실패 (파일 없음\u{00B7}지원하지 않는 포맷\u{00B7}손상)],
  [`5`],  [크기/포맷 불일치. 단일 쌍 `--metrics`, `--diff-out`, `--comp-out` 에서 발생],
  [`6`],  [`--diff-out` 의 FLIP 계산 실패],
  [`7`],  [`--diff-out` 의 PNG 쓰기 실패 (경로 없음\u{00B7}권한\u{00B7}디스크)],
  [`8`],  [`--validate` 에서 NaN 또는 Inf 발견],
  [`10`], [CI 게이트 FAIL. `--fail-psnr`/`--fail-ssim`/`--fail-flip`/`--fail-maxerr` 또는 `--fail-uniformity`/`--fail-semu` 위반],
)

#note[`--pair --metrics` 처럼 여러 프레임을 도는 경우, 개별 프레임의 `mismatch`
는 해당 행만 플레이스홀더로 남기고 전체 종료 코드를 5 로 만들지 않습니다.
exit 5 는 *단일 쌍* 실행에서만 나옵니다. 시퀀스 실행에서는 게이트 결과(0 또는
10)가 종료 코드를 결정합니다.]

== 실전 CI 스크립트

=== (a) bash 회귀 게이트 루프

시퀀스 전체를 게이트에 통과시키고, FAIL 한 프레임만 골라 증거 diff 이미지를
만드는 스크립트입니다. `set -e` 를 쓰지 않는 것이 핵심입니다 \u{2014} exit 10 은
"정상적으로 판정된 실패" 이므로 우리가 직접 처리해야 합니다.

```
#!/usr/bin/env bash
# scripts/av_regression.sh — DDI 보상 IP 회귀 게이트
set -uo pipefail

REF=golden                 # 원본(보상 전) 프레임 디렉토리
OUT=build/ip_out           # 보상 IP 출력 프레임 디렉토리
REPORT=build/metrics.csv
MIN_PSNR=38
mkdir -p build

first=$(ls "$REF"/*.png | head -n 1)
if [ -z "$first" ]; then echo "no reference frames in $REF"; exit 1; fi

# 1) 시퀀스 전체 지표 + 2단계 게이트 (stdout=CSV, stderr=요약은 CI 로그로)
av --pair --metrics "$first" "$OUT" \
   --fail-psnr "$MIN_PSNR" --warn-psnr 42 \
   --fail-ssim 0.985 --fail-flip 0.08 --fail-maxerr 8 \
   > "$REPORT"
rc=$?

case "$rc" in
  0)  echo "== GATE PASS ==" ;;
  10) echo "== GATE FAIL == 아래 프레임이 임계를 넘었습니다:" ;;
  *)  echo "av 실행 실패 (exit $rc)"; exit "$rc" ;;
esac

# 2) FAIL 프레임 추출 — psnr_db(4열)가 숫자이면서 임계 미만인 행만
#    ('inf'/'missing'/'mismatch' 같은 문자열 행은 정규식으로 걸러낸다)
fails=$(awk -F, -v t="$MIN_PSNR" \
        'NR>1 && $4 ~ /^[0-9.]+$/ && $4+0 < t { print $1 }' "$REPORT")

# 3) FAIL 프레임만 증거 diff PNG 생성 (아티팩트 첨부용)
for f in $fails; do
  echo "  - $f"
  av --diff-out "build/diff_$f" --diff-mode signed --amplify 8 --sbs \
     "$REF/$f" "$OUT/$f"
done

# 4) 플랫 필드 균일도도 함께 확인 (별도 게이트, 별도 exit)
if [ -f "$OUT/flat_white.png" ]; then
  av --uniformity "$REF/flat_white.png" "$OUT/flat_white.png" \
     --fail-uniformity 88 --fail-semu 1.5 > build/uniformity.csv
  urc=$?
  [ "$urc" -eq 10 ] && { echo "== UNIFORMITY GATE FAIL =="; rc=10; }
fi

exit "$rc"
```

#tip[`awk` 필터에서 `$4 ~ /^[0-9.]+$/` 를 반드시 넣으세요. `psnr_db` 열에는
완전 일치 프레임의 `inf` 나 플레이스홀더의 `mismatch`/`missing` 문자열이
들어올 수 있는데, awk 구현에 따라 이들이 0 으로 형변환되어 *멀쩡한 프레임을
FAIL 로 오인* 하게 됩니다.]

=== (b) GitHub Actions \u{2014} JUnit 리포트 업로드

`--format junit` 으로 표준 테스트 리포트를 만들고, 게이트가 실패해도 리포트를
아티팩트로 남기는 워크플로입니다. `if: always()` 가 그 역할을 합니다.

```
# .github/workflows/image-quality.yml
name: image-quality

on: [push, pull_request]

jobs:
  metrics:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Build & install av
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j"$(nproc)"
          cmake --install build --prefix "$HOME/.local"
          echo "$HOME/.local/bin" >> "$GITHUB_PATH"

      - name: Generate IP output frames
        run: ./scripts/run_ip.sh golden build/ip_out

      # 헤드리스 — 디스플레이도 GPU 도 필요 없습니다
      - name: Metrics gate (JUnit)
        run: |
          mkdir -p reports
          av --pair --metrics golden/f0001.png build/ip_out \
             --format junit \
             --fail-psnr 38 --fail-ssim 0.985 --fail-flip 0.08 \
             > reports/av-metrics.xml

      - name: Metrics table (CSV, 추세 보관용)
        if: always()
        run: |
          av --pair --metrics golden/f0001.png build/ip_out \
             > reports/av-metrics.csv

      - name: Uniformity gate
        if: always()
        run: |
          av --uniformity golden/flat_white.png build/ip_out/flat_white.png \
             --fail-uniformity 88 --fail-semu 1.5 \
             > reports/av-uniformity.csv

      - name: Upload reports
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: av-reports
          path: |
            reports/av-metrics.xml
            reports/av-metrics.csv
            reports/av-uniformity.csv

      - name: Publish JUnit result
        if: always()
        uses: mikepenz/action-junit-report@v4
        with:
          report_paths: reports/av-metrics.xml
```

동작 방식:

+ `Metrics gate` 스텝이 exit 10 을 내면 그 스텝이 실패하고 잡이 빨간불이 됩니다.
+ 뒤따르는 스텝은 모두 `if: always()` 라 계속 실행되어 CSV/균일도 리포트와
  아티팩트 업로드가 끝까지 진행됩니다.
+ `action-junit-report` 가 `<failure>` 가 붙은 testcase 를 PR 체크에
  프레임 이름 그대로 표시하므로, 어느 프레임이 회귀했는지 PR 화면에서 바로
  보입니다.

#tip[야간 회귀에는 `--comp-batch` 를 한 스텝 더 붙여 FW 레퍼런스와 HW RTL 의
프레임별 `dpsnr`\u{00B7}`win2`/`win3` 추세를 CSV 로 축적해 두면, 특정 커밋에서
어느 블록부터 벌어지기 시작했는지 되짚기 쉽습니다. `worst2`/`worst3` 의
`bx:by@psnr` 좌표를 그대로 GUI 에 넣어 현장 확인하는 흐름은 5장을 참조하세요.]
