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
- [ ] alexws sync-linux → build-offline → bundle → /user/alex/local/bin/av-bundle 설치
- [ ] 설치본 --version/--help 확인

## Review
(완료 후 작성)
