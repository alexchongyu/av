#!/usr/bin/env bash
# ─── AppImage 빌드 스크립트 ────────────────────────────────────────────────────
# 용도: 완전 독립(self-contained) AppImage 생성
#       - 모든 shared library 번들
#       - Mesa swrast(소프트웨어 렌더러) 번들 → GPU 없는 환경, X11 포워딩에서도 동작
#       - 하드웨어 GL 실패 시 자동 소프트웨어 렌더링 폴백
#
# 사용법:
#   bash script/make-appimage.sh              # av 빌드 후 AppImage 생성
#   bash script/make-appimage.sh --no-build   # 이미 빌드된 bin/av 사용
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"
WORK_DIR="$PROJECT_ROOT/build/appimage_work"
APPDIR="$WORK_DIR/AppDir"

# ── 아키텍처 감지 ─────────────────────────────────────────────────────────────
UNAME_M="$(uname -m)"
case "$UNAME_M" in
    aarch64|arm64) LD_ARCH="aarch64" ;;
    *)             LD_ARCH="x86_64"  ;;
esac

LINUXDEPLOY="$WORK_DIR/linuxdeploy-${LD_ARCH}.AppImage"
APPIMAGETOOL="$WORK_DIR/appimagetool-${LD_ARCH}.AppImage"
LD_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${LD_ARCH}.AppImage"
AT_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${LD_ARCH}.AppImage"
OUTPUT="$BIN_DIR/av-${LD_ARCH}.AppImage"
BINARY="$BIN_DIR/av"

# FUSE 없는 환경(일부 서버, 컨테이너)에서도 AppImage 실행되도록
export APPIMAGE_EXTRACT_AND_RUN=1

# ── 인자 파싱 ─────────────────────────────────────────────────────────────────
NO_BUILD=false
for arg in "$@"; do [[ "$arg" == "--no-build" ]] && NO_BUILD=true; done

echo "==> av AppImage 빌드 (arch=${LD_ARCH})"
echo ""

# ── [1/5] av 빌드 ─────────────────────────────────────────────────────────────
if [[ "$NO_BUILD" == false ]]; then
    echo "── [1/5] av 빌드..."
    if [[ -f "$PROJECT_ROOT/av-deps.tar.gz" ]]; then
        bash "$SCRIPT_DIR/build-offline.sh"
    else
        bash "$SCRIPT_DIR/build-linux.sh"
    fi
    echo ""
else
    echo "── [1/5] 빌드 스킵 (--no-build)"
fi

if [[ ! -f "$BINARY" ]]; then
    echo "오류: 바이너리 없음 → $BINARY"
    echo "      먼저 빌드: bash script/build-linux.sh"
    exit 1
fi
echo "    바이너리: $BINARY  ($(du -sh "$BINARY" | cut -f1))"
echo ""

# ── [2/5] 빌드 도구 다운로드 ─────────────────────────────────────────────────
echo "── [2/5] 빌드 도구 준비..."
mkdir -p "$WORK_DIR"

if ! command -v curl &>/dev/null; then
    echo "오류: curl 필요 → sudo apt install curl"; exit 1
fi

for tool_name in linuxdeploy appimagetool; do
    if [[ "$tool_name" == "linuxdeploy" ]]; then
        tool_path="$LINUXDEPLOY"; tool_url="$LD_URL"
    else
        tool_path="$APPIMAGETOOL"; tool_url="$AT_URL"
    fi
    if [[ ! -f "$tool_path" ]]; then
        echo "    다운로드: $tool_name"
        curl -fSL --retry 3 --retry-delay 2 -o "$tool_path" "$tool_url"
        chmod +x "$tool_path"
    else
        echo "    존재: $tool_name"
    fi
done
echo ""

# ── [3/5] AppDir 생성 (linuxdeploy로 deps 수집) ───────────────────────────────
echo "── [3/5] AppDir 생성 및 라이브러리 수집..."
rm -rf "$APPDIR"

# --output 없이 실행 → AppDir만 생성 (패키징은 직접 수행)
"$LINUXDEPLOY" \
    --appdir  "$APPDIR" \
    --executable "$BINARY" \
    --desktop-file "$PROJECT_ROOT/packaging/av.desktop" \
    --icon-file    "$PROJECT_ROOT/packaging/av.svg"

echo ""

# ── [4/5] Mesa swrast 번들 (소프트웨어 렌더러 폴백용) ────────────────────────
echo "── [4/5] Mesa 소프트웨어 렌더러 번들..."

DRI_DEST="$APPDIR/usr/lib/dri"
mkdir -p "$DRI_DEST"

# Mesa DRI 드라이버 검색 경로 (배포판별 차이 대응)
DRI_SEARCH_PATHS=(
    "/usr/lib/${UNAME_M}-linux-gnu/dri"
    "/usr/lib/x86_64-linux-gnu/dri"
    "/usr/lib/aarch64-linux-gnu/dri"
    "/usr/lib/dri"
    "/usr/local/lib/dri"
)

SWRAST_SRC=""
for dir in "${DRI_SEARCH_PATHS[@]}"; do
    if [[ -f "$dir/swrast_dri.so" ]]; then
        SWRAST_SRC="$dir/swrast_dri.so"
        break
    fi
done

if [[ -n "$SWRAST_SRC" ]]; then
    cp "$SWRAST_SRC" "$DRI_DEST/swrast_dri.so"
    echo "    번들: swrast_dri.so  ($(du -sh "$DRI_DEST/swrast_dri.so" | cut -f1))"
else
    echo "    경고: swrast_dri.so 를 찾을 수 없습니다."
    echo "          sudo apt install libgl1-mesa-dri 후 재실행하세요."
    echo "          소프트웨어 렌더링 폴백이 동작하지 않을 수 있습니다."
fi

# kms_swrast (DRM 없는 환경 추가 지원)
for dir in "${DRI_SEARCH_PATHS[@]}"; do
    if [[ -f "$dir/kms_swrast_dri.so" ]]; then
        cp "$dir/kms_swrast_dri.so" "$DRI_DEST/" 2>/dev/null || true
        break
    fi
done

echo ""

# ── [5/5] 커스텀 AppRun 작성 후 appimagetool로 패키징 ────────────────────────
echo "── [5/5] AppRun 작성 및 패키징..."

# 커스텀 AppRun: 하드웨어 GL 실패 시 자동으로 소프트웨어 렌더러 폴백
cat > "$APPDIR/AppRun" <<'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"

# 번들된 라이브러리 우선 사용
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"

# 번들된 Mesa DRI 드라이버 경로 설정
export LIBGL_DRIVERS_PATH="$HERE/usr/lib/dri:${LIBGL_DRIVERS_PATH:-}"

# 이미 소프트웨어 모드로 재실행된 경우 → 바로 실행
if [[ "${_AV_SW_RENDER:-}" == "1" ]]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    exec "$HERE/usr/bin/av" "$@"
fi

# 1차 시도: 하드웨어 렌더링
tmplog=$(mktemp /tmp/av_XXXXXX.log)
"$HERE/usr/bin/av" "$@" 2>"$tmplog"
exit_code=$?

# OpenGL/GLX 관련 오류 감지 → 소프트웨어 렌더링으로 재실행
if [[ $exit_code -ne 0 ]] && \
   grep -qE "GLX|fbConfig|X Error.*BadValue|No matching|OpenGL|glXCreate" "$tmplog" 2>/dev/null; then
    rm -f "$tmplog"
    exec env _AV_SW_RENDER=1 "$0" "$@"
fi

rm -f "$tmplog"
exit $exit_code
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# appimagetool로 최종 패키징
mkdir -p "$BIN_DIR"
ARCH="$LD_ARCH" "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"

# ── 결과 ─────────────────────────────────────────────────────────────────────
if [[ -f "$OUTPUT" ]]; then
    chmod +x "$OUTPUT"
    echo ""
    echo "==> AppImage 완성!"
    echo "    파일: $OUTPUT"
    echo "    크기: $(du -sh "$OUTPUT" | cut -f1)"
    echo ""
    echo "실행:"
    echo "  $OUTPUT [이미지_파일]"
    echo ""
    echo "  GPU 없는 환경, X11 포워딩(XQuartz 등)에서도 자동으로 동작합니다."
else
    echo "오류: AppImage 생성 실패"
    exit 1
fi
