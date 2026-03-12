#!/usr/bin/env bash
# ─── 의존성 사전 다운로드 스크립트 ────────────────────────────────────────────────
# 용도: 인터넷이 되는 머신에서 아래 항목을 모두 다운로드하여 패키징한다.
#       - 빌드 의존성: SDL3, glad, imgui, stb  → av-deps.tar.gz
#       - AppImage 빌드 도구: linuxdeploy, appimagetool → av-appimage-tools.tar.gz
#
# 사용법:
#   bash script/fetch-deps.sh
#
# 결과물:
#   av-deps.tar.gz            — cmake 소스 의존성
#   av-appimage-tools.tar.gz  — AppImage 빌드 도구 (Linux x86_64/aarch64)
#
# 두 파일을 소스 코드와 함께 오프라인 머신으로 복사한 뒤:
#   bash script/build-offline.sh      # 바이너리 빌드
#   bash script/make-appimage.sh      # AppImage 생성
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FETCH_BUILD_DIR="$PROJECT_ROOT/build_fetch_tmp"
DEPS_ARCHIVE="$PROJECT_ROOT/av-deps.tar.gz"
TOOLS_ARCHIVE="$PROJECT_ROOT/av-appimage-tools.tar.gz"
TOOLS_TMP="$PROJECT_ROOT/build_tools_tmp"

# ── [1/3] cmake 소스 의존성 다운로드 ─────────────────────────────────────────
echo "==> [1/3] cmake 소스 의존성 다운로드 (SDL3, glad, imgui, stb)"
echo "    임시 빌드: $FETCH_BUILD_DIR"
echo ""

cmake -B "$FETCH_BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF

echo ""
echo "    소스 디렉토리 확인..."
for name in sdl3 glad imgui stb; do
    src_dir="$FETCH_BUILD_DIR/_deps/${name}-src"
    if [[ -d "$src_dir" ]]; then
        echo "    [OK] ${name}-src"
    else
        echo "    [MISSING] ${name}-src"; exit 1
    fi
done

tar czf "$DEPS_ARCHIVE" -C "$FETCH_BUILD_DIR/_deps" sdl3-src glad-src imgui-src stb-src
echo "    패키지: $DEPS_ARCHIVE  ($(du -sh "$DEPS_ARCHIVE" | cut -f1))"
rm -rf "$FETCH_BUILD_DIR"

# ── [2/3] AppImage 빌드 도구 다운로드 ────────────────────────────────────────
echo ""
echo "==> [2/3] AppImage 빌드 도구 다운로드 (linuxdeploy, appimagetool)"
mkdir -p "$TOOLS_TMP"

# "파일명 URL" 형식으로 나열 (bash 3.2 호환)
TOOL_LIST=(
    "linuxdeploy-x86_64.AppImage   https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    "linuxdeploy-aarch64.AppImage  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage"
    "appimagetool-x86_64.AppImage  https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    "appimagetool-aarch64.AppImage https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-aarch64.AppImage"
    "runtime-x86_64                https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
    "runtime-aarch64               https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-aarch64"
)

for entry in "${TOOL_LIST[@]}"; do
    filename="${entry%% *}"
    url="${entry##* }"
    dest="$TOOLS_TMP/$filename"
    if [[ ! -f "$dest" ]]; then
        echo "    다운로드: $filename"
        curl -fSL --retry 3 --retry-delay 2 -o "$dest" "$url"
    else
        echo "    존재: $filename"
    fi
done

tar czf "$TOOLS_ARCHIVE" -C "$TOOLS_TMP" .
echo "    패키지: $TOOLS_ARCHIVE  ($(du -sh "$TOOLS_ARCHIVE" | cut -f1))"
rm -rf "$TOOLS_TMP"

# ── [3/3] 완료 요약 ──────────────────────────────────────────────────────────
echo ""
echo "==> 완료!"
echo "    $DEPS_ARCHIVE    ($(du -sh "$DEPS_ARCHIVE" | cut -f1))"
echo "    $TOOLS_ARCHIVE   ($(du -sh "$TOOLS_ARCHIVE" | cut -f1))"
echo ""
echo "다음 단계:"
echo "  1. 아래 두 파일을 소스코드와 함께 오프라인 머신으로 복사"
echo "       av-deps.tar.gz"
echo "       av-appimage-tools.tar.gz"
echo "  2. 오프라인 머신에서:"
echo "       bash script/build-offline.sh      # 바이너리 빌드"
echo "       bash script/make-appimage.sh       # AppImage 생성"
