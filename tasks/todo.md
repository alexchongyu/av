# todo — Windows 빌드 (av.exe) via Demura(WSL, 192.168.2.241) (2026-07-22)  ✅ 완료

대상: Mac 소스 → **C: (/mnt/c/Users/Alex/claude_code/av)** → Windows CMake(VS2022/ClangCL) → av.exe
도구: wcmake = cmake.exe -G "Visual Studio 17 2022" -T ClangCL / wmake = devenv.com /build Release

## 진행
- [x] 241 무패스워드 SSH 확인 (Demura, alex)
- [x] script/sync-win.sh 작성 (rsync → C: 경로, VERSION 스냅샷)
- [x] script/win-build-wsl.sh 작성 (interop 자동탐지 + deps + configure + build)
- [x] 소스 sync 실행
- [x] configure (wcmake): CONFIGURE OK — FetchContent(SDL3/glad/imgui/stb), lcms2 없음→color mgmt off
- [x] build (cmake --build Release) → **bin/av.exe (PE32+ GUI x86-64, 3.5MB)**
- [x] av.exe PE 유효성 확인

## 막혔던 문제와 해결
1. **WSL /home UNC**: Windows MSBuild/cmd 가 `\\wsl.localhost\...` current dir 거부 (MSB8066).
   → **C: 경로(/mnt/c/Users/Alex/claude_code/av)로 이동** 후 정상.
2. **비대화형 SSH에 WSL_INTEROP 없음**: Windows .exe 실행 실패(`accept4 failed 110`).
   → `/run/WSL/*_interop` 중 살아있는 소켓 자동 탐지해 export (대화형 WSL 세션이 열려 있어야 함).
3. **glad 코드생성기 jinja2 없음**: Windows Python(C:/Program Files/Python312)에 `pip install --user jinja2`.
4. run_in_background + heredoc-stdin 은 원격에 stdin 미전달 → 스크립트 파일 + nohup 분리실행 + 로그 폴링.

## Review
- av 는 이미 CMakeLists 에 Windows 분기(MSVC 런타임, WIN32_EXECUTABLE, STBI_WINDOWS_UTF8,
  lcms2 optional, %LOCALAPPDATA% 설치)가 있어 소스 수정 없이 빌드됨.
- 산출물: `C:\Users\Alex\claude_code\av\bin\av.exe` (POST_BUILD 로 %LOCALAPPDATA%\av\ 에도 복사).
- 새 스크립트 2개(sync-win.sh, win-build-wsl.sh) 추가. 재빌드 절차:
  `bash script/sync-win.sh` → 241 에서 `bash script/win-build-wsl.sh`.
- 무회귀: 기존 코드/빌드 무변경, Windows 전용 신규 스크립트만 추가.
- 커밋 예정, 푸시는 승인 대기.
