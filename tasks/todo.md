# todo — --zoom 옵션 구현·영속화 + ImGui ini 파일명 변경 (2026-07-21)

## 구현
- [x] app.h: CliOptions에 `bool zoom_set=false` 추가
- [x] app.h: AppState에 `float zoom_setting=0.0f` 추가
- [x] app.cpp: parse_cli `--zoom` 에서 `zoom_set=true` (0/fit=fit, N=배율)
- [x] app.cpp: apply_cli_options 에서 `if(zoom_set) state.zoom_setting=opts.zoom`
- [x] app.cpp: load_app_ini `zoom` 키 읽기
- [x] app.cpp: save_app_ini `zoom` 키 쓰기
- [x] app.cpp: print_help `--zoom` 설명 갱신
- [x] main.cpp: `io.IniFilename` → `.av_imgui.ini`
- [x] main.cpp: 시작 로드 직후 `zoom_setting>0` 이면 viewport_set_zoom+center

## 검증
- [x] 맥 로컬 빌드 성공 (clang, 15/15) + --help/--version 확인
- [ ] `--zoom 1` → 1:1, .av.ini에 zoom=1, 무플래그 재실행도 1:1 (사용자 시각 확인)
- [ ] `--zoom fit`/`--zoom 2` 동작 (사용자 시각 확인)
- [ ] `.av_imgui.ini` 생성 확인, 무회귀(빈 ini → fit)

## 배포
- [x] alexws sync-linux → build-offline(gcc) → bundle → /user/alex/local/bin/av-bundle 설치
- [x] 설치본 --version(v0.22-31-g2055236)/--help(zoom 라인) 확인

## Review
- `--zoom`은 파싱만 되고 뷰에 적용 안 되던 죽은 옵션이었음 → 시작 로드 직후 viewport_set_zoom+center로 배선(메뉴 "1:1 Pixel"과 동일 경로). 값: 0/fit=창맞춤, 1=1:1, N=N배.
- 영속화: `~/.av.ini`에 `zoom=` 추가. CLI --zoom 지정 시 덮어쓰고 저장, 미지정 시 저장값 재사용. 기본 0(fit) → 무회귀.
- ImGui ini: `av_imgui.ini` → `.av_imgui.ini`(main.cpp:341). 둘 다 *.ini gitignore.
- 검증: 맥 clang + 리눅스 gcc 양쪽 컴파일 성공, --help/--version 정상. 실제 1:1 렌더·저장 왕복은 디스플레이 필요 → 사용자 시각 확인 대기.
- 커밋 2055236(맥). origin/master 미푸시(승인 대기). 리눅스 설치 완료.
