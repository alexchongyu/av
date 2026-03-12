#!/usr/bin/env bash
# ─── VNC 설치 및 av 실행 스크립트 ────────────────────────────────────────────────
# 사용법: bash script/start-vnc.sh [이미지파일]
# 사전 조건: ~/tigervnc-debs/*.deb  (macOS에서 scp로 복사)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEBS_DIR="$HOME/tigervnc-debs"
LOCAL="$HOME/local"
VNC_DISPLAY=":1"
VNC_PORT="5901"
VNC_PASS="avview"
APPIMAGE="$PROJECT_ROOT/bin/av-x86_64.AppImage"

# ── [1/4] Xvnc 바이너리 확인 (이미 설치된 경우 추출 생략) ─────────────────────
echo "[1/4] Xvnc 확인..."
XVNC=$(find "$LOCAL" -name "Xvnc" -type f 2>/dev/null | head -1)
VNCPASSWD=$(find "$LOCAL" -name "vncpasswd" -type f 2>/dev/null | head -1)

if [[ -z "$XVNC" ]]; then
    echo "    Xvnc 없음 → .deb 추출 시도..."
    if [[ ! -d "$DEBS_DIR" ]] || [[ -z "$(ls "$DEBS_DIR"/*.deb 2>/dev/null)" ]]; then
        echo "오류: $DEBS_DIR 에 .deb 파일도 없습니다."
        exit 1
    fi
    mkdir -p "$LOCAL"
    for deb in "$DEBS_DIR"/*.deb; do
        dpkg-deb -x "$deb" "$LOCAL/" 2>/dev/null || true
    done
    XVNC=$(find "$LOCAL" -name "Xvnc" -type f 2>/dev/null | head -1)
    VNCPASSWD=$(find "$LOCAL" -name "vncpasswd" -type f 2>/dev/null | head -1)
fi

if [[ -z "$XVNC" ]]; then
    echo "오류: Xvnc 바이너리를 찾을 수 없습니다. ($LOCAL)"
    exit 1
fi
echo "    Xvnc: $XVNC"

# ── [2/4] 비밀번호 설정 ───────────────────────────────────────────────────────
echo "[2/4] VNC 비밀번호 설정 (비밀번호: $VNC_PASS)..."
mkdir -p "$HOME/.vnc"
if [[ -n "$VNCPASSWD" ]]; then
    printf '%s\n' "$VNC_PASS" | "$VNCPASSWD" -f > "$HOME/.vnc/passwd"
    chmod 600 "$HOME/.vnc/passwd"
    echo "    완료"
else
    echo "    vncpasswd 없음 - SecurityTypes None 사용"
fi

# ── [3/4] Xvnc 시작 ──────────────────────────────────────────────────────────
echo "[3/4] Xvnc 시작..."

# 이미 실행 중이면 종료
if [[ -f "/tmp/.X${VNC_DISPLAY#:}-lock" ]]; then
    echo "    기존 Xvnc 종료..."
    kill "$(cat "/tmp/.X${VNC_DISPLAY#:}-lock")" 2>/dev/null || true
    sleep 1
fi

# 라이브러리 경로
export LD_LIBRARY_PATH="\
$LOCAL/usr/lib/x86_64-linux-gnu:\
$LOCAL/usr/lib:\
${LD_LIBRARY_PATH:-}"

# XKB 경로
XKB="$LOCAL/usr/share/X11/xkb"
[[ ! -d "$XKB" ]] && XKB="/usr/share/X11/xkb"

# 폰트 경로 (없으면 -fp 옵션 제외)
FONT_PATH="$LOCAL/usr/share/fonts/X11/misc"
[[ ! -d "$FONT_PATH" ]] && FONT_PATH="/usr/share/fonts/X11/misc"
FP_OPT=()
[[ -d "$FONT_PATH" ]] && FP_OPT=(-fp "$FONT_PATH")

if [[ -f "$HOME/.vnc/passwd" ]]; then
    "$XVNC" "$VNC_DISPLAY" \
        -rfbport "$VNC_PORT" \
        -rfbauth "$HOME/.vnc/passwd" \
        -geometry 1920x1080 \
        -depth 24 \
        -xkbdir "$XKB" \
        "${FP_OPT[@]}" \
        &>/tmp/Xvnc.log &
else
    "$XVNC" "$VNC_DISPLAY" \
        -rfbport "$VNC_PORT" \
        -SecurityTypes None \
        -geometry 1920x1080 \
        -depth 24 \
        -xkbdir "$XKB" \
        "${FP_OPT[@]}" \
        &>/tmp/Xvnc.log &
fi

# 시작 대기
for i in $(seq 1 10); do
    if [[ -f "/tmp/.X${VNC_DISPLAY#:}-lock" ]]; then
        echo "    시작 완료 (${i}초)"
        break
    fi
    sleep 1
    if [[ $i -eq 10 ]]; then
        echo "오류: Xvnc 시작 실패. 로그:"
        cat /tmp/Xvnc.log
        exit 1
    fi
done

# ── [4/4] av 실행 ─────────────────────────────────────────────────────────────
echo "[4/4] av 실행..."
if [[ ! -f "$APPIMAGE" ]]; then
    echo "오류: $APPIMAGE 없음"
    exit 1
fi

LOCAL_IP=$(hostname -I | awk '{print $1}')
echo ""
echo "==> 준비 완료!"
echo "    접속: vnc://${LOCAL_IP}:${VNC_PORT}"
echo "    비밀번호: ${VNC_PASS}"
echo ""

DISPLAY="$VNC_DISPLAY" \
LIBGL_ALWAYS_SOFTWARE=1 \
APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGE" "${1:-}"
