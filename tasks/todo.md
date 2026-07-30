# todo — `--comp` 3-영상 블록 비교 모드 (2026-07-30, 마감 30분)

## 요구사항 (사용자 스펙)
- [x] 1. `--comp` CLI 옵션 + `--blk 8x8|10x10|16x16…` (기본 8x8)
- [x] 2. 입력 3장: img1(원본, 왼쪽) img2(중간) img3(오른쪽) — 3장이면 diff 모드 disable
- [x] 3. info 창에 PSNR 2개: img1↔img2, img1↔img3
- [x] 4. PSNR 최악(worst) 블록 상위 N개 컬러 사각형 테두리, `--num_blk N` (기본 16)
- [x] 5. 블록 hover 풍선말: 블록 PSNR/MSE, 채널별 MSE, 오차픽셀 수, 최악픽셀 orig/this/Δ
- [x] 6a. Mac 빌드+검증+인스톨 (v0.22-63-ga9b95a9)
- [x] 6b. alexws(Linux) 빌드+인스톨 (/user/alex/local/bin/av, v0.22-63)
- [x] 6c. demura(Windows) 빌드+인스톨 (bin+LOCALAPPDATA+C:\Windows, md5 8026efca 3위치 일치)

## Review
- 설계: 2-이미지 코어(std::array<ImageEntry,2>)는 불변. img3는 CompState.img_c에 분리
  보관하고 렌더 순간에만 std::swap(O(1))으로 슬롯 1에 끼워 A|B|Diff 3패널 코드를 재사용.
  comp 모드에서 diff/blink/overlay/swap 매 프레임 강제 해제 → 무회귀.
- 검증(수치): comp 시작 요약 stdout — 전역 PSNR·worst 블록 수·#1 블록의 grid/MSE/PSNR이
  독립 Python 구현과 자리수까지 일치 (45.2636/31.8142 dB; img2 오차블록 5=주입 수와 일치,
  #1 grid(20,10) mse 333.8958). --metrics 회귀 정상, CLI 에러 4경로 정상.
- 검증(육안): 사용자가 3-패널 GUI 직접 확인 ("잘되는 것 같다").
- 커밋: 733136d(기능) + a9b95a9(startup 요약). 푸시는 지시 대기.
- 한계: --software 렌더 경로에도 오버레이 후킹은 넣었으나 육안 확인은 GL 경로만.
  3번째 패널 hover 시 Shift+V 콜러메트리 풍선말은 A/B 값을 표시(C 아님) — 추후 개선 후보.
