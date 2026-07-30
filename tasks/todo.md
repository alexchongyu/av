# todo — `--comp` 3-영상 블록 비교 모드 (2026-07-30, 마감 30분)

## 요구사항 (사용자 스펙)
- [ ] 1. `--comp` CLI 옵션 + `--blk 8x8|10x10|16x16…` (기본 8x8)
- [ ] 2. 입력 3장: img1(원본, 왼쪽) img2(중간) img3(오른쪽) — 3장이면 diff 모드 disable
- [ ] 3. info 창에 PSNR 2개: img1↔img2, img1↔img3
- [ ] 4. PSNR 최악(worst) 블록 상위 N개 컬러 사각형 테두리 표시, `--num_blk N` (기본 16)
- [ ] 5. 블록 hover 시 풍선말: 블록 PSNR/MSE, 최악 픽셀 위치/오차, 채널별 오차 등 상세
- [ ] 6. Mac 빌드+검증 → alexws(Linux)·demura(Windows) 컴파일/인스톨

## 실행 계획
- [ ] Recon 워크플로우 (4 병렬: CLI/레이아웃/info/풍선말)
- [ ] 구현 (메인 루프 직접 — 파일 겹침이 커서 병렬 편집 금지)
- [ ] Mac 빌드 + 검증 (스크린샷)
- [ ] 커밋 (푸시는 지시 없어 보류)
- [ ] Linux + Windows 배포 (백그라운드 병렬)

## Review
(작업 후 기록)

---
(이전 작업: 4기능+cheat sheet+macOS 검은화면 수정 — 전부 완료·푸시됨, git log 참조)
