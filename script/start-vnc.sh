#!/usr/bin/env bash
# ─── Xvnc 시작 스크립트 (VNC 연결 테스트용) ─────────────────────────────────
# 사용법: bash script/start-vnc.sh
# 사전 조건: ~/tigervnc-debs/*.deb  (macOS에서 scp로 복사) 또는 ~/local/에 기존 설치
set -euo pipefail

DEBS_DIR="$HOME/tigervnc-debs"
LOCAL="$HOME/local"
VNC_DISPLAY=":1"
VNC_DISP_NUM="${VNC_DISPLAY#:}"
VNC_PORT="5901"
VNC_PASS="avview"

# ── [1/3] Xvnc 바이너리 확인 ─────────────────────────────────────────────────
echo "[1/3] Xvnc 확인..."
XVNC=$(find "$LOCAL" -name "Xvnc" -type f 2>/dev/null | head -1)
VNCPASSWD=$(find "$LOCAL" -name "vncpasswd" -type f 2>/dev/null | head -1)

_extract_debs() {
    if [[ ! -d "$DEBS_DIR" ]] || [[ -z "$(ls "$DEBS_DIR"/*.deb 2>/dev/null)" ]]; then
        echo "오류: $DEBS_DIR 에 .deb 파일이 없습니다."
        return 1
    fi
    mkdir -p "$LOCAL"
    for deb in "$DEBS_DIR"/*.deb; do
        dpkg-deb -x "$deb" "$LOCAL/" 2>/dev/null || true
    done
}

if [[ -z "$XVNC" ]]; then
    echo "    Xvnc 없음 → .deb 추출 시도..."
    _extract_debs || exit 1
    XVNC=$(find "$LOCAL" -name "Xvnc" -type f 2>/dev/null | head -1)
    VNCPASSWD=$(find "$LOCAL" -name "vncpasswd" -type f 2>/dev/null | head -1)
elif [[ -z "$VNCPASSWD" ]]; then
    echo "    vncpasswd 없음 → .deb 추출 재시도..."
    _extract_debs || true
    VNCPASSWD=$(find "$LOCAL" -name "vncpasswd" -type f 2>/dev/null | head -1)
fi

if [[ -z "$XVNC" ]]; then
    echo "오류: Xvnc 바이너리를 찾을 수 없습니다. ($LOCAL)"
    exit 1
fi
echo "    Xvnc: $XVNC"

# ── [2/3] 비밀번호 설정 ───────────────────────────────────────────────────────
echo "[2/3] VNC 비밀번호 설정 (비밀번호: $VNC_PASS)..."
mkdir -p "$HOME/.vnc"
USE_AUTH=false

# vncpasswd 시도: -f 플래그로 stdin → stdout
if [[ -n "$VNCPASSWD" ]]; then
    printf '%s\n%s\n' "$VNC_PASS" "$VNC_PASS" | "$VNCPASSWD" -f > "$HOME/.vnc/passwd" 2>/dev/null || true
    PASSWD_SIZE=$(wc -c < "$HOME/.vnc/passwd" 2>/dev/null | tr -d ' ' || echo 0)
    if [[ "$PASSWD_SIZE" -eq 8 ]]; then
        chmod 600 "$HOME/.vnc/passwd"
        USE_AUTH=true
        echo "    완료 (vncpasswd, ${PASSWD_SIZE}바이트)"
    else
        echo "    vncpasswd 출력 오류 (${PASSWD_SIZE}바이트) → openssl 시도..."
    fi
fi

# openssl fallback: VNC passwd = 8바이트 패딩 후 고정키 DES-ECB 암호화
# des-cbc + zero IV = des-ecb (단일 8바이트 블록)
# 키 e84ad660c4721ae0 = TigerVNC 고정키의 비트 반전 (표준 DES용)
if [[ "$USE_AUTH" != "true" ]] && command -v openssl &>/dev/null; then
    # 8바이트 null-padded 패스워드를 hex로 변환 (파이프 SIGPIPE 회피)
    _pw_hex=$(printf '%s' "$VNC_PASS" | od -A n -t x1 | tr -d ' \n')
    _pw_hex="${_pw_hex}0000000000000000"
    _pw_hex="${_pw_hex:0:16}"  # 8바이트 = 16 hex chars
    _try_openssl() {
        echo "$_pw_hex" | xxd -r -p | \
            openssl enc -des-cbc -nopad -nosalt \
            -K e84ad660c4721ae0 -iv 0000000000000000 "$@" 2>/dev/null
    }
    # 시도 1: 기본
    _try_openssl > "$HOME/.vnc/passwd" || true
    PASSWD_SIZE=$(wc -c < "$HOME/.vnc/passwd" 2>/dev/null | tr -d ' ' || echo 0)
    # 시도 2: OpenSSL 3.x legacy provider
    if [[ "$PASSWD_SIZE" -ne 8 ]]; then
        _try_openssl -provider legacy -provider default > "$HOME/.vnc/passwd" || true
        PASSWD_SIZE=$(wc -c < "$HOME/.vnc/passwd" 2>/dev/null | tr -d ' ' || echo 0)
    fi
    if [[ "$PASSWD_SIZE" -eq 8 ]]; then
        chmod 600 "$HOME/.vnc/passwd"
        USE_AUTH=true
        echo "    완료 (openssl, ${PASSWD_SIZE}바이트)"
    else
        echo "    openssl 출력 오류 (${PASSWD_SIZE}바이트)"
    fi
    unset -f _try_openssl
fi

if [[ "$USE_AUTH" != "true" ]]; then
    echo "    경고: 패스워드 생성 실패 - SecurityTypes None 사용 (macOS 연결 불가)"
fi

# ── GDM XDMCP 확인 (읽기 전용) ────────────────────────────────────────────────
_check_gdm_xdmcp() {
    local conf="/etc/gdm3/custom.conf"
    [[ ! -f "$conf" ]] && conf="/etc/gdm/custom.conf"
    [[ ! -f "$conf" ]] && return 1
    grep -q '^\s*Enable\s*=\s*true' "$conf" 2>/dev/null
}

# ── [3/3] Xvnc 시작 ──────────────────────────────────────────────────────────
echo "[3/3] Xvnc 시작..."

# GDM XDMCP 상태 확인 (읽기 전용) → 항상 -query localhost 시도
if _check_gdm_xdmcp; then
    echo "    GDM XDMCP 활성화 확인됨 → -query localhost 사용"
else
    echo "    GDM XDMCP 상태 미확인 → -query localhost 시도 (실패 시 fallback)"
fi

# stale lock 파일 정리 (프로세스가 죽은 경우)
LOCK_FILE="/tmp/.X${VNC_DISP_NUM}-lock"
X11_SOCK="/tmp/.X11-unix/X${VNC_DISP_NUM}"
if [[ -f "$LOCK_FILE" ]]; then
    LOCK_PID=$(cat "$LOCK_FILE" 2>/dev/null | tr -d ' ' || echo "")
    if [[ -n "$LOCK_PID" ]] && kill -0 "$LOCK_PID" 2>/dev/null; then
        echo "    기존 Xvnc (PID $LOCK_PID) 종료..."
        kill "$LOCK_PID" 2>/dev/null || true
        sleep 1
    else
        echo "    stale lock 파일 정리 (PID ${LOCK_PID:-?} 없음)..."
        rm -f "$LOCK_FILE"
        rm -f "$X11_SOCK"
    fi
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

# 항상 -query localhost 사용 (GDM XDMCP 로그인 화면)
if [[ "$USE_AUTH" == "true" ]]; then
    "$XVNC" "$VNC_DISPLAY" \
        -rfbport "$VNC_PORT" \
        -SecurityTypes VncAuth \
        -rfbauth "$HOME/.vnc/passwd" \
        -localhost no \
        -geometry 1920x1080 \
        -depth 24 \
        -xkbdir "$XKB" \
        "${FP_OPT[@]}" \
        -query localhost \
        &>/tmp/Xvnc.log &
else
    "$XVNC" "$VNC_DISPLAY" \
        -rfbport "$VNC_PORT" \
        -SecurityTypes None \
        -localhost no \
        -geometry 1920x1080 \
        -depth 24 \
        -xkbdir "$XKB" \
        "${FP_OPT[@]}" \
        -query localhost \
        &>/tmp/Xvnc.log &
fi
XVNC_PID=$!

# 시작 대기
for i in $(seq 1 10); do
    if [[ -f "$LOCK_FILE" ]]; then
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

# XDMCP 연결 실패 감지 → fallback 세션
sleep 2
if ! kill -0 "$XVNC_PID" 2>/dev/null; then
    echo "    XDMCP 연결 실패 (Xvnc 종료됨) → fallback 세션으로 재시작..."

    # -query localhost 없이 재시작
    rm -f "$LOCK_FILE" "$X11_SOCK"
    if [[ "$USE_AUTH" == "true" ]]; then
        "$XVNC" "$VNC_DISPLAY" \
            -rfbport "$VNC_PORT" \
            -SecurityTypes VncAuth \
            -rfbauth "$HOME/.vnc/passwd" \
            -localhost no \
            -geometry 1920x1080 \
            -depth 24 \
            -xkbdir "$XKB" \
            "${FP_OPT[@]}" \
            &>/tmp/Xvnc.log &
    else
        "$XVNC" "$VNC_DISPLAY" \
            -rfbport "$VNC_PORT" \
            -SecurityTypes None \
            -localhost no \
            -geometry 1920x1080 \
            -depth 24 \
            -xkbdir "$XKB" \
            "${FP_OPT[@]}" \
            &>/tmp/Xvnc.log &
    fi
    XVNC_PID=$!

    # 재시작 대기
    for i in $(seq 1 10); do
        if [[ -f "$LOCK_FILE" ]]; then
            echo "    fallback Xvnc 시작 완료 (${i}초)"
            break
        fi
        sleep 1
        if [[ $i -eq 10 ]]; then
            echo "오류: fallback Xvnc 시작 실패. 로그:"
            cat /tmp/Xvnc.log
            exit 1
        fi
    done

    export DISPLAY="$VNC_DISPLAY"
    echo "    fallback: 직접 세션 시작 시도..."
    if command -v gnome-session &>/dev/null; then
        gnome-session &>/tmp/vnc-desktop.log &
        echo "    세션: gnome-session (PID $!)"
    elif command -v openbox &>/dev/null; then
        openbox &>/tmp/vnc-desktop.log &
        echo "    세션: openbox (PID $!)"
    elif command -v xterm &>/dev/null; then
        xterm -geometry 132x43+0+0 -fa 'Monospace' -fs 12 &>/tmp/vnc-desktop.log &
        echo "    세션: xterm (PID $!)"
    else
        echo "    경고: 사용 가능한 세션/WM 없음"
    fi
fi

# ── 진단 정보 출력 ─────────────────────────────────────────────────────────────
echo ""
echo "=== 진단 정보 ==="

# 프로세스 확인
if kill -0 "$XVNC_PID" 2>/dev/null; then
    echo "  Xvnc PID: $XVNC_PID (실행 중)"
else
    echo "  경고: Xvnc (PID $XVNC_PID) 이미 종료됨!"
fi

# 로그 출력 (내용이 있으면)
if [[ -s /tmp/Xvnc.log ]]; then
    echo "  로그 (/tmp/Xvnc.log):"
    sed 's/^/    /' /tmp/Xvnc.log
fi

# 포트 리슨 확인
echo "  포트 ${VNC_PORT} 리슨 확인:"
if command -v ss &>/dev/null; then
    ss -tlnp 2>/dev/null | grep ":${VNC_PORT}" | sed 's/^/    /' || echo "    (리슨 없음)"
elif command -v netstat &>/dev/null; then
    netstat -tlnp 2>/dev/null | grep ":${VNC_PORT}" | sed 's/^/    /' || echo "    (리슨 없음)"
else
    echo "    (ss/netstat 없음 - 확인 불가)"
fi

# 접속 정보
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "?.?.?.?")
echo ""
echo "==> 접속 준비 완료!"
echo "    주소: vnc://${LOCAL_IP}:${VNC_PORT}"
echo "    비밀번호: ${VNC_PASS}"
echo ""
echo "Ctrl+C 로 종료"

# Ctrl+C 시 Xvnc 정리
trap 'echo ""; echo "Xvnc 종료 중..."; kill "$XVNC_PID" 2>/dev/null || true; exit 0' INT TERM

wait "$XVNC_PID"
