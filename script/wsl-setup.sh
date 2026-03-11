#!/usr/bin/env bash
# ─── WSL Ubuntu 빌드 환경 설정 스크립트 ────────────────────────────────────────
# 용도: WSL Ubuntu (22.04 / 24.04)에서 av 프로젝트 빌드에 필요한 패키지 설치
# 사용법: bash script/wsl-setup.sh
set -euo pipefail

echo "==> av WSL 빌드 환경 설정 시작"

# ── 1. apt 패키지 업데이트 및 설치 ───────────────────────────────────────────
echo ""
echo "── [1/3] 시스템 패키지 업데이트 중..."
sudo apt update

echo ""
echo "── [2/3] 빌드 의존성 패키지 설치 중..."
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libgl-dev \
    libegl-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxss-dev \
    libwayland-dev \
    libxkbcommon-dev \
    libdbus-1-dev \
    libibus-1.0-dev \
    liblcms2-dev

# ── 2. CMake 버전 확인 ────────────────────────────────────────────────────────
echo ""
echo "── [3/3] CMake 버전 확인..."
CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)

echo "    현재 CMake 버전: $CMAKE_VERSION"

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 24 ]); then
    echo "    !! CMake 3.24 미만 감지 — 업그레이드 필요"
    echo ""
    echo "    업그레이드 방법 (하나 선택):"
    echo "      방법 A: snap  → sudo snap install cmake --classic"
    echo "      방법 B: pip   → pip install --upgrade cmake"
    echo ""
    read -rp "    snap으로 자동 업그레이드하시겠습니까? [y/N] " answer
    if [[ "$answer" =~ ^[Yy]$ ]]; then
        sudo snap install cmake --classic
        echo "    CMake 업그레이드 완료: $(cmake --version | head -n1)"
    else
        echo "    수동으로 업그레이드 후 다시 시도하세요."
        exit 1
    fi
else
    echo "    OK: CMake $CMAKE_VERSION >= 3.24"
fi

echo ""
echo "==> WSL 빌드 환경 설정 완료!"
echo ""
echo "다음 단계:"
echo "  cd /path/to/av"
echo "  bash script/build-linux.sh"
