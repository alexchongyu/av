#!/usr/bin/env bash
# ─── 의존성 사전 다운로드 스크립트 ────────────────────────────────────────────────
# 용도: 인터넷이 되는 머신에서 의존성(SDL3, glad, imgui, stb)을 미리 다운로드하여
#       오프라인 머신에서 빌드할 수 있도록 패키징한다.
#
# 사용법:
#   bash script/fetch-deps.sh
#
# 결과물:
#   av-deps.tar.gz  — 프로젝트 루트에 생성
#   이 파일을 소스 코드와 함께 오프라인 머신으로 복사한 뒤
#   script/build-offline.sh 로 빌드하면 된다.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FETCH_BUILD_DIR="$PROJECT_ROOT/build_fetch_tmp"
ARCHIVE="$PROJECT_ROOT/av-deps.tar.gz"

echo "==> 의존성 다운로드 시작 (cmake configure only)"
echo "    프로젝트: $PROJECT_ROOT"
echo "    임시 빌드: $FETCH_BUILD_DIR"
echo ""

# cmake configure: FetchContent가 각 라이브러리 소스를 다운로드한다
# (실제 컴파일은 하지 않음 — 소스 디렉토리 생성이 목적)
cmake -B "$FETCH_BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF

echo ""
echo "==> 소스 디렉토리 확인..."
for name in sdl3 glad imgui stb; do
    src_dir="$FETCH_BUILD_DIR/_deps/${name}-src"
    if [[ -d "$src_dir" ]]; then
        echo "    [OK] ${name}-src"
    else
        echo "    [MISSING] ${name}-src — 다운로드 실패"
        exit 1
    fi
done

echo ""
echo "==> 패키징: $ARCHIVE"
# -src 디렉토리만 패키징 (빌드 아티팩트 제외 → 플랫폼 독립적)
tar czf "$ARCHIVE" \
    -C "$FETCH_BUILD_DIR/_deps" \
    sdl3-src \
    glad-src \
    imgui-src \
    stb-src

ARCHIVE_SIZE=$(du -sh "$ARCHIVE" | cut -f1)
echo ""
echo "==> 완료!"
echo "    파일: $ARCHIVE  ($ARCHIVE_SIZE)"
echo ""
echo "다음 단계:"
echo "  1. av-deps.tar.gz 와 소스 코드를 오프라인 머신으로 복사"
echo "  2. 오프라인 머신에서: bash script/build-offline.sh"
echo ""

# 임시 빌드 디렉토리 정리
rm -rf "$FETCH_BUILD_DIR"
echo "    (임시 빌드 디렉토리 삭제 완료)"
