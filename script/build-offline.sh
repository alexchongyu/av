#!/usr/bin/env bash
# ─── 오프라인 빌드 스크립트 ───────────────────────────────────────────────────────
# 용도: 인터넷이 없는 머신에서 av를 빌드한다.
#       av-deps.tar.gz (fetch-deps.sh로 생성)가 프로젝트 루트에 있어야 한다.
#
# 사용법:
#   bash script/build-offline.sh            # Release 빌드 (기본값)
#   bash script/build-offline.sh debug      # Debug 빌드
#   bash script/build-offline.sh clean      # build/ 삭제 후 재빌드
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
ARCHIVE="$PROJECT_ROOT/av-deps.tar.gz"
BUILD_TYPE="Release"

# 인자 처리
ARG="${1:-}"
if [[ "$ARG" == "debug" ]]; then
    BUILD_TYPE="Debug"
elif [[ "$ARG" == "clean" ]]; then
    echo "==> build/ 디렉토리 삭제 중..."
    rm -rf "$BUILD_DIR"
fi

echo "==> av 오프라인 빌드 시작 (BUILD_TYPE=$BUILD_TYPE)"
echo "    프로젝트: $PROJECT_ROOT"
echo ""

# ── 의존성 아카이브 확인 ──────────────────────────────────────────────────────────
if [[ ! -f "$ARCHIVE" ]]; then
    echo "오류: av-deps.tar.gz 를 찾을 수 없습니다."
    echo "      인터넷이 되는 머신에서 먼저 실행하세요:"
    echo "      bash script/fetch-deps.sh"
    exit 1
fi

# ── 의존성 배치 ───────────────────────────────────────────────────────────────────
DEPS_DIR="$BUILD_DIR/_deps"
# 이미 압축 해제되어 있으면 스킵
if [[ -d "$DEPS_DIR/sdl3-src" && -d "$DEPS_DIR/glad-src" && \
      -d "$DEPS_DIR/imgui-src" && -d "$DEPS_DIR/stb-src" ]]; then
    echo "==> 의존성 소스 이미 존재 — 압축 해제 스킵"
else
    echo "==> 의존성 압축 해제 중..."
    mkdir -p "$DEPS_DIR"
    tar xzf "$ARCHIVE" -C "$DEPS_DIR/"
    echo "    완료"
fi

echo ""
echo "── 의존성 소스 확인..."
for name in sdl3 glad imgui stb; do
    if [[ -d "$DEPS_DIR/${name}-src" ]]; then
        echo "    [OK] ${name}-src"
    else
        echo "    [MISSING] ${name}-src"
        exit 1
    fi
done

# ── 스테일 CMakeCache 제거 (다른 머신에서 만들어진 캐시) ─────────────────────────────
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
if [[ -f "$CACHE_FILE" ]]; then
    CACHED_SRC=$(grep "^CMAKE_HOME_DIRECTORY" "$CACHE_FILE" 2>/dev/null | cut -d= -f2 || true)
    if [[ -n "$CACHED_SRC" && "$CACHED_SRC" != "$PROJECT_ROOT" ]]; then
        echo "==> 다른 머신의 캐시 감지 ($CACHED_SRC) — 캐시 삭제 후 재configure"
        rm -f "$CACHE_FILE"
        rm -rf "$BUILD_DIR/CMakeFiles"
    fi
fi

# ── CMake Configure (오프라인 모드) ───────────────────────────────────────────────
echo ""
echo "── cmake configure (오프라인 모드)..."
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON

# ── 빌드 ──────────────────────────────────────────────────────────────────────────
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
else
    echo "오류: 빌드 실패 — $BINARY 를 찾을 수 없습니다."
    exit 1
fi
