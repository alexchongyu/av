#!/usr/bin/env bash
# ─── 완전 정적 빌드 스크립트 ────────────────────────────────────────────────────
# 용도: CentOS 6.x 등 오래된 glibc 환경에서 실행 가능한 바이너리를 생성한다.
#       glibc, libstdc++, X11 등 모든 라이브러리를 정적 링크한다.
#
# 사전 조건 (빌드 머신):
#   - static 라이브러리 설치 필요:
#     RHEL/CentOS: yum install glibc-static libX11-devel libXext-devel mesa-libGL-devel
#     Ubuntu/Debian: apt install libc6-dev libx11-dev libxext-dev libgl-dev
#   - av-deps.tar.gz가 프로젝트 루트에 있어야 한다 (오프라인 빌드 시)
#
# 사용법:
#   bash script/build-static.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-static"

echo "==> av 완전 정적 빌드 시작"
echo "    프로젝트: $PROJECT_ROOT"
echo ""

# ── 의존성 확인 ──────────────────────────────────────────────────────────────────
ARCHIVE="$PROJECT_ROOT/av-deps.tar.gz"
DEPS_DIR="$BUILD_DIR/_deps"

if [[ -f "$ARCHIVE" ]]; then
    if [[ -d "$DEPS_DIR/sdl3-src" && -d "$DEPS_DIR/glad-src" && \
          -d "$DEPS_DIR/imgui-src" && -d "$DEPS_DIR/stb-src" ]]; then
        echo "==> 의존성 소스 이미 존재 — 압축 해제 스킵"
    else
        echo "==> 의존성 압축 해제 중..."
        mkdir -p "$DEPS_DIR"
        tar xzf "$ARCHIVE" -C "$DEPS_DIR/"
    fi
    OFFLINE_FLAG="-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
else
    echo "==> av-deps.tar.gz 없음 — 온라인 빌드"
    OFFLINE_FLAG=""
fi

# ── 스테일 캐시 제거 ─────────────────────────────────────────────────────────────
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
if [[ -f "$CACHE_FILE" ]]; then
    CACHED_SRC=$(grep "^CMAKE_HOME_DIRECTORY" "$CACHE_FILE" 2>/dev/null | cut -d= -f2 || true)
    if [[ -n "$CACHED_SRC" && "$CACHED_SRC" != "$PROJECT_ROOT" ]]; then
        echo "==> 다른 머신의 캐시 감지 — 캐시 삭제"
        rm -f "$CACHE_FILE"
        rm -rf "$BUILD_DIR/CMakeFiles"
    fi
fi

# ── CMake Configure ──────────────────────────────────────────────────────────────
echo ""
echo "── cmake configure (정적 빌드)..."
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAV_FULL_STATIC=ON \
    $OFFLINE_FLAG

# ── 빌드 ────────────────────────────────────────────────────────────────────────
echo ""
echo "── cmake build ($(nproc) 코어)..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# ── 결과 확인 ────────────────────────────────────────────────────────────────────
BINARY="$PROJECT_ROOT/bin/av"
if [[ -f "$BINARY" ]]; then
    echo ""
    echo "==> 빌드 성공!"
    echo "    바이너리: $BINARY"
    echo "    크기: $(du -sh "$BINARY" | cut -f1)"
    echo ""
    echo "── 동적 라이브러리 의존성 확인:"
    ldd "$BINARY" 2>&1 || echo "    (정적 바이너리 — 동적 의존성 없음)"
    echo ""
    echo "── glibc 요구 버전 확인:"
    objdump -T "$BINARY" 2>/dev/null | grep GLIBC | sed 's/.*GLIBC_/GLIBC_/' | sort -uV | tail -5 || echo "    (glibc 의존성 없음)"
else
    echo "오류: 빌드 실패 — $BINARY 를 찾을 수 없습니다."
    exit 1
fi
