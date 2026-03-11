#!/usr/bin/env bash
# ─── Linux / WSL 빌드 스크립트 ─────────────────────────────────────────────────
# 용도: av 프로젝트를 Linux (WSL 포함)에서 빌드
# 사용법:
#   bash script/build-linux.sh            # Release 빌드 (기본값)
#   bash script/build-linux.sh debug      # Debug 빌드
#   bash script/build-linux.sh clean      # build/ 디렉토리 삭제 후 재빌드
set -euo pipefail

# ── 설정 ──────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BUILD_TYPE="Release"

# 인자 처리
ARG="${1:-}"
if [[ "$ARG" == "debug" ]]; then
    BUILD_TYPE="Debug"
elif [[ "$ARG" == "clean" ]]; then
    echo "==> build/ 디렉토리 삭제 중..."
    rm -rf "$BUILD_DIR"
fi

echo "==> av Linux 빌드 시작 (BUILD_TYPE=$BUILD_TYPE)"
echo "    프로젝트 루트: $PROJECT_ROOT"

# ── 환경 사전 점검 ────────────────────────────────────────────────────────────
echo ""
echo "── 환경 점검..."

# CMake 버전 확인
if ! command -v cmake &>/dev/null; then
    echo "오류: cmake를 찾을 수 없습니다. script/wsl-setup.sh를 먼저 실행하세요."
    exit 1
fi
CMAKE_VER=$(cmake --version | head -n1 | awk '{print $3}')
echo "    CMake: $CMAKE_VER"

# Git 확인 (FetchContent에 필요)
if ! command -v git &>/dev/null; then
    echo "오류: git을 찾을 수 없습니다."
    exit 1
fi
echo "    git: $(git --version | awk '{print $3}')"

# OpenGL 헤더 확인
if ! dpkg -l libgl-dev &>/dev/null 2>&1; then
    echo "경고: libgl-dev가 설치되지 않을 수 있습니다. cmake가 실패하면 script/wsl-setup.sh 실행."
fi

# WSL 감지 및 디스플레이 확인
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "    WSL 환경 감지됨"
    if [[ -z "${DISPLAY:-}" ]]; then
        echo "    경고: DISPLAY 변수가 설정되지 않았습니다."
        echo "    WSLg가 동작 중이라면 자동으로 설정됩니다."
        echo "    미동작 시: export DISPLAY=:0"
    else
        echo "    DISPLAY=$DISPLAY"
    fi
fi

# ── CMake Configure ───────────────────────────────────────────────────────────
echo ""
echo "── cmake configure..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -S "$PROJECT_ROOT"

# ── 빌드 ─────────────────────────────────────────────────────────────────────
echo ""
echo "── cmake build ($(nproc) 코어 사용)..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# ── 결과 확인 ────────────────────────────────────────────────────────────────
BINARY="$PROJECT_ROOT/bin/av"
if [[ -f "$BINARY" ]]; then
    echo ""
    echo "==> 빌드 성공!"
    echo "    바이너리: $BINARY"
    echo "    크기: $(du -sh "$BINARY" | cut -f1)"
    echo ""
    echo "실행 방법:"
    echo "  $BINARY [이미지_파일]"
    echo ""
    echo "  또는 ~/.local/bin/av 로 자동 설치되었다면:"
    echo "  av [이미지_파일]"
else
    echo "오류: 빌드 실패 — $BINARY 를 찾을 수 없습니다."
    exit 1
fi
