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

## 확장 6종 (2026-07-31 오전, 사용자 컨펌 후 구현 — c485a58)
- [x] ① Ctrl+W 승패 맵 (블록별 img2 vs img3, |Δ|>1dB, 3패널)
- [x] ② 풍선말 상대 알고리즘 병기 + echo 패널 자체 PSNR 태그 (원본 패널은 박스만)
- [x] ③ Tab/Shift+Tab worst 블록 순회 (병합 심각도순, 자동 센터+줌, SEL 박스)
- [x] ④ --comp-out 헤드리스 블록 CSV/JSON — Python 정답과 자리수 일치 검증
- [x] ⑤ Ctrl+G 블록 PSNR 히트맵 (노랑→빨강, fit 줌 스트라이드 집계)
- [x] ⑦ ',' 3-way 블링크 (orig→img2→img3, phase 라벨, </> 간격)
- [x] ;/a 시퀀스 3-영상 파일명 미러 (comp_mirror_bc; 원본이 시퀀스 구동)
- [x] help 창(Comp/CLI Headless) + 치트시트 갱신 (문서 동기화 규칙 메모리 저장)
- [x] echo 패널 상세 풍선말 (hover와 동일 형식, 자기 통계 — comp_scan_block 온디맨드)
- [x] 3플랫폼 배포 (v0.22-75-g0f24dd3) 후 자동 push (26b63a9..0f24dd3, 새 규칙 첫 적용)

## 확장 2차 (2026-07-31 오전, 전부 사용자 컨펌 — v0.22-78-g3675a0c)
- [x] [/] img2 · Shift+[/] img3 패널별 worst 순회 (Tab=병합 유지)
- [x] Ctrl+D worst 블록 리스트 창 (행 클릭 → 3패널 점프)
- [x] Statusbar comp 요약 (P2/P3/W2/W3/T 상시)
- [x] --blk-metric rgb|y|chroma (검증: metric별 #1 역전) + 풍선말 rank metric 표기
- [x] 스파이크 하이브리드 선정 75%+25% (마젠타 P 박스; 검증 12+4)
- [x] --comp-batch 프레임별 요약 CSV (검증: 스왑 미러 정확)
- [x] S=img2↔img3 스왑, G=그리드(Ctrl+N에서 이동), --grid [WxH|N] 기본 16x16
- [x] win-install.sh: C:\Windows 무UAC 배포 기본화 (av_sys_install 태스크)
- [x] 3플랫폼 v0.22-78 배포 + 자동 push

## 추가 요청 (2026-07-31)
- [x] Ctrl/Cmd+B: --comp에서 blk_w x blk_h 블록 그리드 바운더리 표시/숨김 토글
      (3패널 공통, 밝은 시안 라인, 화면상 블록 < 4px면 생략; worst rect는 그대로)
- [x] Ctrl/Cmd+B → worst 블록 사각형 토글로 정정, 그리드는 Ctrl/Cmd+N으로 이동(보존)
      — 사용자 수정 반영, lessons.md에 "기존 요소 토글이 기본 해석" 교훈 기록
- [x] Mac 인스톨 (v0.22-67-g3522707)
- [x] Linux 인스톨 (v0.22-67; gcc ICE→-j2, NFS busy rsync→--delete 없이 재동기)
- [x] Windows bin+LOCALAPPDATA (md5 d4eea26f 일치, v0.22-67)
- [ ] Windows C:\Windows\av.exe — UAC 승인 대기 (현재 v0.22-63; WSL interop 불안정으로
      팝업 발사 보류. 사용자 조치: 관리자 cmd에서 install-sys.cmd 실행 또는 재발사 요청)

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

## 2026-07-31 세션 잔여 작업 (다음 세션 최우선)

- [x] **demura(Windows) 재배포 완료** — 실은 세션 종료 직전 빌드가 성공해 있었고
      (`/tmp/wbuild.log` = `EXIT=0`) install 단계만 안 돌았던 것. 이후 정상 재빌드까지 확인.
      → 3곳 md5 `afcf8ffc…` = v0.22-84-ga3e335c
- [ ] **치트시트 오류 수정** (매뉴얼 집필 중 소스 대조로 발견)
  - [x] `0`/`Space` 중복 해소 — 사용자 결정으로 **코드 변경**: `Space` = fit,
        `0` = 1:1(100%). 핫키 창·View 메뉴 힌트·치트시트·매뉴얼 모두 동기화
  - [x] `1`~`8`은 `2×`~`256×` (치트시트의 "1×~256×" 표기 오류)
  - [x] ini 경로는 `av.ini`가 아니라 **`~/.av.ini`**
  - [x] JPEG **EXIF 방향 자동 적용은 미구현** (stb_image에 해당 코드 없음)
  - [x] `Ctrl+D`는 diff 끄기가 아니라 **diff 픽셀 리스트 창 토글**
  - [x] 지원 확장자 실체는 8종, `.pnm` 확장자는 목록에 없음
- [x] Magnifier 후속: SSIM/FLIP GL 패널은 fake ImageEntry(pixels 비어 있음)라
      확대경이 검게 뜬다. `!is_diff_panel && img.pixels.empty()`면 생략하는 가드 추가 검토
- [ ] overlay/blend·curtain 및 SW heatmap 경로는 확대경 호출 자체가 없음 (기존 동작)

### 완료 (2026-07-31)
- [x] Magnifier 전 패널 동시 표시 (`b439fee`) — Mac·Linux 설치 완료, push 완료
- [x] AV User's Manual 263쪽 typst+PDF (`6670fc3`) — 11장 + cetz 그림 13개

### 완료 (2026-08-01)
- [x] Windows 재배포 — 3플랫폼 모두 `v0.22-84-ga3e335c`
- [x] `Space` = fit-to-window 로 변경 (`0` = 1:1 유지) + 문서 4곳 동기화
- [x] 치트시트 오류 6건 수정 (Ctrl+D · JPEG EXIF · ~/.av.ini · 1~8 배율 · PNM 확장자 · 0/Space)
- [x] Magnifier: SSIM/FLIP 가짜 엔트리 패널은 확대경 생략 (검은 사각형 방지)
