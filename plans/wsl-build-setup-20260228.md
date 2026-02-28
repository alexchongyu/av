# 플랜: WSL Ubuntu Linux에서 av 프로젝트 컴파일

날짜: 2026-02-28

## 구현 결과

### 생성된 파일

1. **`script/wsl-setup.sh`** — WSL Ubuntu 빌드 환경 설정 (패키지 설치 + CMake 버전 확인)
2. **`script/build-linux.sh`** — Linux/WSL 빌드 스크립트 (configure + build)

### CMakeLists.txt 수정: 불필요

- `elseif(UNIX)` 분기 (line 146-148): `find_package(OpenGL REQUIRED)` + `OpenGL::GL` — OK
- macOS 전용 코드 (`CMAKE_OSX_*`, `-framework`): `if(APPLE)` 블록 안에 격리 — Linux에서 자동 스킵
- `#ifdef __APPLE__` (main.cpp line 164-167): forward compatible flag — Linux에서 자동 스킵
- 외부 라이브러리 (SDL3, glad, ImGui, stb): FetchContent 자동 다운로드 — OK

## WSL 빌드 순서

### Step 1: 환경 설정 (최초 1회)
```bash
bash script/wsl-setup.sh
```
- apt 패키지 설치: build-essential, cmake, git, libgl-dev, libx11-dev, libwayland-dev 등
- CMake 3.24 미만이면 snap 업그레이드 자동 제안

### Step 2: 빌드
```bash
bash script/build-linux.sh          # Release 빌드
bash script/build-linux.sh debug    # Debug 빌드
bash script/build-linux.sh clean    # 클린 빌드
```

### Step 3: 실행
```bash
./bin/av [이미지_파일]
# 또는 자동 설치된 경우
av [이미지_파일]
```

## WSL 주의사항

- **WSL2 + Windows 11 필요**: WSLg 기본 탑재 → GUI 앱 실행 가능
- **DISPLAY 변수**: WSLg 정상 동작 시 자동 설정됨. 미설정 시 `export DISPLAY=:0`
- **FetchContent**: SDL3/glad/ImGui/stb를 git clone하므로 인터넷 연결 필요 (최초 빌드 시)
- **빌드 시간**: 최초 빌드 시 외부 라이브러리 다운로드 + 컴파일로 5-10분 소요 예상
