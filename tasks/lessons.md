# Lessons Learned — av 프로젝트

## 구조체 이동 시 anonymous namespace 처리

**상황**: chart_windows.cpp의 anonymous namespace 안에 정의된 구조체와 static 함수들을
chart_export.h/cpp로 이동할 때 발생하는 중복 정의 문제.

**해결책**:
- 구조체는 chart_export.h에 global namespace에 정의
- chart_windows.cpp의 anonymous namespace에서 struct 정의 + static compute 함수 제거
- draw_stats_table 함수만 anonymous namespace에 유지 (render_stats_window에서 호출)
- chart_export.h를 chart_windows.cpp 상단에 include

## stb 헤더 구현 파일 (stb_impl.cpp) 패턴

- `STB_TRUETYPE_IMPLEMENTATION`은 반드시 stb_impl.cpp에 한 곳에서만 정의
- soft_renderer.cpp에서는 `#include <stb_truetype.h>`만 (IMPLEMENTATION 없이)
- stb_image_write.h는 이미 stb_impl.cpp에 있으므로 soft_renderer.cpp에서 include 가능

## SDL 파일 다이얼로그 패턴

- `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`는 비동기 콜백 방식
- userdata struct를 `new`로 heap 할당하고, 콜백에서 `delete`
- 콜백 내에서만 pending_path 설정, 실제 처리는 다음 프레임 render() 시작에서

## ImGui PushFont/PopFont 위치

- `ImGui::Begin()`이 실패해도 (창이 접힌 경우) PopFont는 필요
- 패턴: `PushFont` → `Begin` → (content) → `End` → `PopFont` (Begin 결과와 무관하게)

## CMakeLists.txt 소스 파일 추가

- 새 .cpp 파일은 AV_SOURCES에 반드시 추가
- 타겟 컴파일 정의 (IMGUI_FONT_DIR 등)는 av 타겟에 연결된 모든 소스에서 사용 가능

## 차트 Export 버튼 중복 ID 방지

- ImGui SmallButton 등은 동일한 레이블을 가지면 ID 충돌 발생
- `##suffix` 형식으로 고유 ID 지정 필수 (예: `"PNG A##ha"`, `"PNG B##hb"`)

## 새 기능 추가 패턴 (2026-03-05)

### AppState 확장 패턴
- 새 state 구조체를 app.h에 정의 후 AppState에 필드로 추가
- 구조체에 기본값 지정 (`bool active = false;` 등)

### 새 분석 창 추가 패턴
1. `chart_export.h/cpp`에 데이터 구조체 + 추출 함수 추가
2. `chart_windows.h`에 render 함수 선언
3. `chart_windows.cpp`에 구현 (draw_stats_table 재활용 가능)
4. `app.h` AppState에 `show_xxx` bool 추가
5. `app.cpp` handle_keyboard에 단축키 추가
6. `main_window.cpp` 메뉴에 MenuItem 추가 + render 함수 호출

### 새 렌더링 모드 추가 패턴 (Overlay/Blend)
1. `shader_sources.h`에 새 GLSL 셰이더 추가
2. `image_panel.h`에 새 ShaderProgram 멤버 + render 메서드 선언
3. `image_panel.cpp` init()에서 새 셰이더 컴파일
4. `image_panel.cpp` render()에서 조건부로 새 렌더 메서드 호출
5. `main_window.cpp` 레이아웃 분기에 새 모드 추가

### ROI 선택 패턴
- ROI 모드 ON → left drag에서 pan 대신 ROI 선택
- 드래그 완료 시 화면 좌표 → 이미지 픽셀 좌표 변환 (기존 s2ix/s2iy 패턴)
- ROI 오버레이: 이미지 픽셀 → 화면 좌표 역변환으로 사각형 그리기

### 시퀀스 탐색 패턴
- 이미지 로드 시 자동으로 `scan_image_directory()` 호출 → SequenceState 갱신
- N/Shift+N 키로 current_index 변경 → open_state.open_pending 설정
- main_window.cpp의 open_pending 처리에서 자동으로 로드 + 시퀀스 재스캔

### open_state 재사용 패턴
- 파일 다이얼로그뿐 아니라 시퀀스 탐색에서도 open_state.opened_path/open_pending 사용 가능
- clear_other=false 설정으로 다른 슬롯 보존

## 무회귀(No-Regression) 절대 원칙 (2026-06-24)

**사용자 지시 (최우선)**: "no regression 이어야 한다. 기능적으로 문제가 안 생겨야 한다.
새로운 버그를 절대 만들지 마라. 철저하게 해라."

### 적용 방법론
1. **베이스라인 확보**: 수정 전에 빌드 성공 + `test/`의 SPR1/SPR3 등으로 동작 캡처
   (export PNG/CSV는 픽셀/바이트 단위로 before/after 비교 가능).
2. **원자적 커밋**: finding 하나당 커밋 하나. 회귀 발생 시 bisect로 즉시 격리.
3. **빌드+검증 게이트**: 각 변경 후 빌드 → 관련 동작 재현 → 정상 케이스가 깨지지 않았는지 확인.
   증명 없이 "완료" 표시 금지.
4. **behavior-preserving 리팩터링 ≠ behavior-changing 버그수정**: 절대 한 커밋에 섞지 않는다.
   리팩터링은 출력이 **픽셀 단위로 동일**해야 함(앱 자체 export로 대조).

### 핵심 함정 — 좌표 변환 통합(H1)
- 현재 `sample_pixel`은 `(int)` 캐스트, `crosshair`/`magnifier`는 `std::floor` 사용 →
  top/left 가장자리에서 이미 1px 불일치 존재.
- 통합 시 "floor로 표준화"하면 그 자체가 **출력 변경(회귀 위험)**임.
- 따라서: 통합 헬퍼는 각 호출부의 **기존 동작을 그대로 보존**해야 하며,
  floor 표준화는 별도의 '명시적 버그수정' 커밋으로 분리하고 사용자 승인 후 진행.

### 커밋/푸시 규칙
- 커밋은 자율적으로 진행 가능. **푸시는 반드시 사용자 승인** 후에만(사용자가 보고 결정).

## "미리보기" 명령이 실제로 실행되는 함정 (2026-06-24)

**사고**: 사용자가 "설치 전 디렉토리 알려주고 승인받아라"고 했는데,
`cmake --install build 2>&1 | head -1 >/dev/null  # not run; placeholder`
라고 적어 **주석과 달리 명령이 실제 실행**되어 승인 전에 설치가 돼버렸다.

**근본 원인**: 상태 변경 명령을 "미리보기/플레이스홀더"라며 쉘에 넣으면 그냥 실행된다.
출력을 `>/dev/null`로 버려도 부작용(설치/푸시/삭제)은 일어난다.

**규칙**:
- 승인이 필요한 상태변경 명령(install, push, rm, mv, 외부 전송)은 **승인 전까지 쉘에 절대 넣지 않는다**.
- 위치/효과를 "미리보기"할 땐 명령을 **실행하지 말고**, 설정/매니페스트를 *읽어서*(grep/cat) 텍스트로만 보여준다.
- 주석으로 "not run"이라고 쓰는 것은 안전장치가 아니다. 명령은 주석을 읽지 않는다.

## 새 CLI 플래그/키 추가 전 "기존 이름 충돌" 필수 확인 (2026-07-19)

**상황**: 사용자가 `--sync` 신규 구현을 요청했으나, `--sync`는 이미 "뷰포트 동기화"로
존재(S키 토글·INI 저장·--help 문서화)했다. `p` 키도 이미 pathfinder에 바인딩돼 있었다.

**규칙**:
- 새 CLI 플래그/단축키/INI 키를 추가하기 전 **반드시 grep으로 기존 사용처를 먼저 확인**한다
  (`grep -n '"--flag"' src`, `SDL_SCANCODE_X`, INI key). 요청 이름이 이미 쓰이면 그대로 덮지 말 것.
- 충돌 시 **사용자에게 결정을 물어본다**(회귀 위험이 있는 개명 vs 신규 이름). 이번엔 `--pair` 신설로 해결.
- 기존 키를 조건부로만 가로채 무회귀 유지: `p`는 "info 창 + 양쪽 로드"일 때만 PSNR, 그 외 pathfinder 보존.

## 두 기능이 한 파일의 인접 라인을 공유할 때 원자 커밋 분리 (2026-07-19)

**상황**: `--pair`와 PSNR 두 기능이 `image_loader.cpp`의 로드-성공 리셋 라인에서 인접
(각각 `panel_missing_msg.clear()` / `info_psnr_computed=false`). 각 커밋이 독립 빌드돼야 bisect 가능.

**해결**: C2 전용 편집 5곳을 **잠시 되돌려 C1을 먼저 빌드·검증·커밋**한 뒤, C2를 복원해 빌드·커밋.
patch hunk 수술보다 안전(각 커밋이 실제로 컴파일됨을 보장). 필드 선언과 그 사용은 같은 커밋에 둔다.

## 렌더 프레임 도중 텍스처 업로드 시 GL_UNPACK 상태 오염 (2026-07-19)

**증상**: `--pair`(또는 일반) 탐색 시, command line 초기 영상은 정상인데 **네비게이션으로
로드한 영상부터 화면이 깨짐**. 역방향도 깨짐.

**근본 원인**: 이미지 로드(→`glTexImage2D`)가 `MainWindow::render()` 안의 deferred-open
처리에서 일어난다. 이 시점엔 ImGui/SDL가 `GL_UNPACK_ROW_LENGTH`를 0이 아닌 값(폰트 아틀라스
폭 등)으로 남겨둔 상태 → `glTexImage2D`가 클라이언트 버퍼를 잘못된 stride로 읽어 힙 over-read
→ 텍스처 손상. 초기 로드는 이벤트 루프 **전**(상태 깨끗)이라 정상.
- ImGui 백엔드 주석에 근거: "setting GL_UNPACK_ROW_LENGTH ... because SDL changes it"
  / UpdateTexture에서 `glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width)`.

**재현/증명 기법 (헤드리스)**: 화면을 못 띄우는 환경에서
1. `sequence_navigate` + 로드 로직을 옮긴 순수 시뮬레이터로 **파일 페어링/인덱스 로직이 정상**임을 먼저 증명(범위 축소).
2. `-fsanitize=address` 빌드 + 메인 루프에 임시 self-test(합성 `;` 주입)로 자동 네비게이션 →
   ASan이 `glTexImage2D`의 OOB를 **정확한 스택으로** 포착. (Apple GL 드라이버 `glgVectorCopy` BUS)
3. pair/단일 모두 재현됨을 확인 → "pair 특유"가 아니라 "렌더 도중 업로드" 일반 문제로 판정.
4. 수정 후 동일 self-test로 **크래시 소멸** 검증. self-test는 커밋 전 제거.

**규칙**:
- **모든 `glTexImage2D`(픽셀 데이터 업로드) 앞에서 픽셀-언팩 상태를 명시적으로 초기화**하라:
  `glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0)`, `GL_UNPACK_ROW_LENGTH=0`, `GL_UNPACK_ALIGNMENT=1`,
  `GL_UNPACK_SKIP_ROWS/PIXELS=0`. 주변(ImGui/SDL) GL 상태를 절대 신뢰하지 말 것.
- FBO용 `nullptr` 업로드는 픽셀 읽기가 없어 무관.
- ASan이 서드파티 GPU 드라이버에서 BUS를 내면 우리 코드의 호출 프레임(스택 #6~)이 진짜 단서다.

## 원격 리눅스 빌드 머신(alexws)은 로그인 셸이 tcsh — ssh 명령은 bash로 감싼다 (2026-07-21)

**상황**: `ssh 192.168.2.2 '... 2>&1 ...'` 가 `Ambiguous output redirect` 로 실패. 원격 로그인
셸이 csh/tcsh라 bash 리다이렉션 문법이 통하지 않음. (`ssh host 'bash -l' < script` 는 정상.)

**규칙**:
- 원격 빌드/설치 명령은 **항상** `ssh host 'bash -l' <<'EOF' ... EOF` 또는 `ssh host bash -lc '...'`.
  `bash -l` 은 로그인 프로필을 읽어 cmake/ninja PATH 까지 확보.
- 오프라인 박스이므로 `build-linux.sh`(온라인) 말고 `build-offline.sh`(FETCHCONTENT_FULLY_DISCONNECTED=ON).
- 빌드 전 스테일 `.git` 제거해야 `git describe` 가 옛 버전을 씌우지 않음(→VERSION.txt 사용).
- 설치는 번들 디렉토리(`av`=래퍼 스크립트) 통째로 배포. 단일 파일 복사 금지.
- **전체 절차는 리포 루트 `linux-compile-install.md` 참조.**

## Windows 빌드(av.exe) via Demura WSL — 3대 함정 (2026-07-22)

빌드 머신 = 192.168.2.241(Demura, Windows PC의 WSL, 무패스워드 SSH). 툴은 사용자 alias
`wcmake`(cmake.exe -G "Visual Studio 17 2022" -T ClangCL), `wmake`(devenv.com /build Release).

1. **반드시 C: 경로에서 빌드** (`/mnt/c/Users/Alex/claude_code/av`). WSL `/home` 은
   `\\wsl.localhost\...` UNC 라 MSBuild custom build(cmd.exe)가 `MSB8066 / "CMD does not
   support UNC paths"` 로 실패(특히 FetchContent 하위빌드). → sync-win.sh 는 C: 로 복사.
2. **WSL_INTEROP**: 비대화형 SSH 세션엔 unset → Windows .exe 실행 시 `accept4 failed 110`.
   `/run/WSL/*_interop` 중 살아있는 소켓 자동 탐지해 export. 대화형 WSL 세션이 열려 있어야 소켓이 삶.
3. **glad = jinja2 필요**: glad2 코드 생성기가 Python+jinja2 사용. Windows Python
   (CMakeCache `_Python_EXECUTABLE`, 여기선 C:/Program Files/Python312)에 `pip install --user jinja2`.
   안 하면 `ModuleNotFoundError: No module named 'jinja2'` → glad_gl 빌드 실패.

- 원격 장시간 빌드 구동법: `run_in_background`+heredoc-stdin 은 stdin 미전달로 실패.
  → 스크립트를 파일로 scp → `nohup bash script build > log 2>&1 &` 분리실행 → 원격 log 폴링.
- CMakeLists 는 이미 Windows 분기 완비(소스 무변경). lcms2 는 Windows에 없어 color mgmt 자동 off.
- 스크립트: `script/sync-win.sh`, `script/win-build-wsl.sh` [all|deps|configure|build].

## Linux(alexws) 빌드 — ninja "manifest still dirty" = 시계 오차 (2026-07-22)

**증상**: sync 후 `build-offline.sh` 가 `ninja: error: manifest 'build.ninja' still dirty
after 100 tries, perhaps system time is not set` 로 실패(컴파일 시작도 못 함).

**원인**: alexws(192.168.2.2)의 **시스템 시계가 맥보다 ~6분 느림**. rsync 가 맥의 mtime을
보존 → 소스 파일이 박스 시각 기준 **미래**. cmake 가 build.ninja 재생성해도 입력(미래)이 여전히
build.ninja(박스 now)보다 최신 → 무한 재생성 루프.

**⚠ 함정(내가 실제로 밟음)**: 트리 전체를 **고정 과거일(예: 07-20)** 로 touch 하면 ninja
dirty-loop 은 사라지지만, **소스 mtime 이 기존 오브젝트(.o, 지난 빌드=박스시각)보다 과거**가 되어
**ninja 가 재컴파일을 건너뛴다**. 결과: 버전 스탬프(version.h는 재생성)만 새것이고 **av 코드는 옛것**
→ "빌드했는데 동작이 예전 그대로"(예: --zoom 유지 안 됨)로 오진되기 쉬움. (Mac/Win 은 정상 → 리눅스만 옛 동작.)

**올바른 해결(둘 중 하나)**:
1. `find . -not -path './.git/*' -exec touch {} +`  (인자 없는 plain touch = **박스 현재시각**).
   → 미래 아님(loop 없음) + 지난 오브젝트보다 최신(재컴파일됨). 고정 과거일(-t)로 하지 말 것.
2. 오브젝트를 지워 강제 재컴파일: `rm -rf build/CMakeFiles/av.dir && ninja -C build`.
근본 해결은 박스 시계 교정(`sudo timedatectl set-ntp true`, sudo 필요).
**검증**: 빌드 후 `grep "zoom_setting > 0.0f" src/image_loader.cpp` 가 아니라, 바이너리가
실제로 재컴파일됐는지(ninja 출력에 소스 컴파일 라인 있는지) 확인할 것. 버전 문자열만으론 불충분.

---

## Windows: C:\Windows UAC 상승 복사 — 대화형 interop 소켓으로 발사할 것

**증상**: SSH→WSL interop 으로 `Start-Process -Verb RunAs` 상승 복사를 돌리면 **UAC 팝업이 데스크톱에 안 뜬다**("팝업 안뜬다"). `-Wait` 는 리턴하는데 파일은 그대로(복사 무효). elevated 프로세스 콘솔 출력은 interop 이 삼켜서 stdout 으론 결과를 못 봄.

**원인**: `/run/WSL/*_interop` 에 여러 소켓이 있고, `1_interop`(및 기타 시스템/비대화형 세션)은 `cmd /c ver` 라이브니스 테스트는 통과하지만 **데스크톱에 UAC UI 를 못 띄운다**. 첫 번째 살아있는 소켓을 잡는 옛 `win-install.sh` 는 이 시스템 소켓을 골라 조용히 no-op.

**해결**: 하나만 고르지 말고 **살아있는 모든 interop 소켓에 상승 복사를 발사**(dead 는 `cmd /c ver` 로 스킵) → 사용자 대화형 세션 소켓에서만 팝업이 뜬다(승인 → 복사 성공). 이번에 통한 건 `37_interop`(높은 번호 = 최근 대화형 세션), `1_interop` 아님. **검증은 stdout 이 아니라 대상 파일 size/mtime 으로** (elevated 출력은 유실). 상세 재사용 스니펫은 메모리 `av-windows-uac-interop-socket` 참조.
