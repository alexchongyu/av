#!/usr/bin/env bash
# ─── VNC 서버 설치 및 실행 스크립트 ──────────────────────────────────────────────
# 용도: tigervnc-debs/ 패키지를 로컬에 설치하고 Xvnc를 시작한 뒤 av를 실행한다.
#       sudo 불필요 — 모든 파일은 ~/local/ 에 설치됨.
#
# 사용법:
#   bash script/start-vnc.sh [이미지파일]
#
# 사전 조건:
#   - ~/tigervnc-debs/*.deb 파일이 있어야 함 (macOS에서 scp로 복사)
#   - bin/av-x86_64.AppImage 가 빌드되어 있어야 함
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DEBS_DIR="$HOME/tigervnc-debs"
LOCAL_DIR="$HOME/local"
VNC_DISPLAY=":1"
VNC_PORT="5901"
VNC_GEOMETRY="1920x1080"
VNC_DEPTH="24"
APPIMAGE="$PROJECT_ROOT/bin/av-x86_64.AppImage"
IMAGE_ARG="${1:-}"

# ── [1/4] .deb 패키지 추출 ────────────────────────────────────────────────────
echo "── [1/4] 패키지 준비..."

if [[ ! -d "$DEBS_DIR" ]]; then
    echo "오류: $DEBS_DIR 디렉토리가 없습니다."
    echo "      macOS에서 먼저 실행하세요:"
    echo "        scp -r tigervnc-debs/ $(whoami)@$(hostname):~/"
    exit 1
fi

mkdir -p "$LOCAL_DIR"
for deb in "$DEBS_DIR"/*.deb; do
    pkg=$(dpkg-deb -f "$deb" Package)
    if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii"; then
        echo "    스킵 (설치됨): $pkg"
    else
        echo "    추출: $pkg"
        dpkg-deb -x "$deb" "$LOCAL_DIR/"
    fi
done
echo ""

# ── [2/4] VNC 비밀번호 설정 ───────────────────────────────────────────────────
echo "── [2/4] VNC 비밀번호..."
mkdir -p "$HOME/.vnc"

VNCPASSWD="$LOCAL_DIR/usr/bin/vncpasswd"
if [[ ! -f "$VNCPASSWD" ]]; then
    # 시스템에 설치된 vncpasswd 사용
    VNCPASSWD="$(command -v vncpasswd 2>/dev/null || true)"
fi

if [[ ! -f "$HOME/.vnc/passwd" ]]; then
    if [[ -n "$VNCPASSWD" && -f "$VNCPASSWD" ]]; then
        echo "    VNC 비밀번호를 설정하세요 (최소 6자):"
        "$VNCPASSWD" "$HOME/.vnc/passwd"
    else
        echo "    vncpasswd 없음 — 비밀번호 없이 실행 (로컬 전용)"
    fi
else
    echo "    기존 비밀번호 사용: $HOME/.vnc/passwd"
fi
echo ""

# ── [3/4] Xvnc 시작 ──────────────────────────────────────────────────────────
echo "── [3/4] Xvnc 시작 (display=${VNC_DISPLAY}, port=${VNC_PORT})..."

# 이미 실행 중인지 확인
LOCK_FILE="/tmp/.X${VNC_DISPLAY#:}-lock"
if [[ -f "$LOCK_FILE" ]]; then
    echo "    Xvnc 이미 실행 중 (display=${VNC_DISPLAY})"
else
    XVNC="$LOCAL_DIR/usr/bin/Xvnc"
    if [[ ! -f "$XVNC" ]]; then
        XVNC="$(command -v Xvnc 2>/dev/null || true)"
    fi

    if [[ -z "$XVNC" || ! -f "$XVNC" ]]; then
        echo "오류: Xvnc 바이너리를 찾을 수 없습니다."
        exit 1
    fi

    # Xvnc 실행 (xkb 경로 명시)
    XKB_DIR="$LOCAL_DIR/usr/share/X11/xkb"
    [[ ! -d "$XKB_DIR" ]] && XKB_DIR="/usr/share/X11/xkb"

    if [[ -f "$HOME/.vnc/passwd" ]]; then
        "$XVNC" "${VNC_DISPLAY}" \
            -geometry "$VNC_GEOMETRY" \
            -depth "$VNC_DEPTH" \
            -rfbauth "$HOME/.vnc/passwd" \
            -rfbport "$VNC_PORT" \
            -xkbdir "$XKB_DIR" \
            -fp "$LOCAL_DIR/usr/share/fonts/X11/misc/,$LOCAL_DIR/usr/share/fonts/X11/Type1/" \
            &>/tmp/Xvnc${VNC_DISPLAY}.log &
    else
        "$XVNC" "${VNC_DISPLAY}" \
            -geometry "$VNC_GEOMETRY" \
            -depth "$VNC_DEPTH" \
            -SecurityTypes None \
            -rfbport "$VNC_PORT" \
            -xkbdir "$XKB_DIR" \
            -fp "$LOCAL_DIR/usr/share/fonts/X11/misc/,$LOCAL_DIR/usr/share/fonts/X11/Type1/" \
            &>/tmp/Xvnc${VNC_DISPLAY}.log &
    fi

    sleep 2

    if [[ -f "$LOCK_FILE" ]]; then
        echo "    Xvnc 시작 완료"
    else
        echo "오류: Xvnc 시작 실패. 로그:"
        cat /tmp/Xvnc${VNC_DISPLAY}.log
        exit 1
    fi
fi
echo ""

# ── [4/4] av 실행 ─────────────────────────────────────────────────────────────
echo "── [4/4] av 실행..."

if [[ ! -f "$APPIMAGE" ]]; then
    echo "오류: $APPIMAGE 없음. 먼저 빌드하세요:"
    echo "      bash script/make-appimage.sh"
    exit 1
fi

echo ""
echo "==> 준비 완료!"
echo "    macOS에서 접속: vnc://$(hostname -I | awk '{print $1}'):${VNC_PORT}"
echo "    또는: Finder → 이동 → 서버에 연결 → vnc://IP:${VNC_PORT}"
echo ""

DISPLAY="${VNC_DISPLAY}" LIBGL_ALWAYS_SOFTWARE=1 \
    APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGE" $IMAGE_ARG
